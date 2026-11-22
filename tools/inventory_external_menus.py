#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Tom Needham
"""Inventory an externally supplied HD DVD menu corpus without retaining its bytes.

The JSON separates byte-level/XML observations from conservative inferences. It
never writes corpus content to the source tree and intentionally assigns no HDi
opcode or undocumented AACS-field semantics.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

TIME_RE = re.compile(r"^\d+:[0-5]\d:[0-5]\d:\d+$")
REF_NAMES = {"src", "href", "ref", "uri", "onEnd"}
XML_START = re.compile(br"<\?xml\b")
ROOT_START = re.compile(br"<(?![!?/])([A-Za-z_][\w.-]*(?::[A-Za-z_][\w.-]*)?)\b")


def qname(name):
    if name.startswith("{"):
        namespace, local = name[1:].split("}", 1)
        return namespace, local
    return "", name


def xml_payload(data):
    start_match = XML_START.search(data)
    if not start_match:
        return None
    start = start_match.start()
    declaration_end = data.find(b">", start)
    root_match = ROOT_START.search(data, declaration_end + 1)
    if not root_match:
        return None
    root_name = root_match.group(1)
    close = re.compile(br"</" + re.escape(root_name) + br"\s*>", re.I)
    endings = list(close.finditer(data, root_match.end()))
    if not endings:
        return None
    return data[start : endings[-1].end()]


def analyze_xml(data, logical_name):
    payload = xml_payload(data)
    if payload is None:
        return None
    try:
        root = ET.fromstring(payload)
    except ET.ParseError:
        return None
    elements = Counter()
    attributes = defaultdict(Counter)
    namespaces = Counter()
    time_attributes = Counter()
    reference_attributes = Counter()
    relationships = Counter()
    for parent in root.iter():
        namespace, local = qname(parent.tag)
        elements[local] += 1
        if namespace:
            namespaces[namespace] += 1
        for raw_name, value in parent.attrib.items():
            attr_namespace, attr = qname(raw_name)
            attributes[local][attr] += 1
            if attr_namespace:
                namespaces[attr_namespace] += 1
            if TIME_RE.match(value):
                time_attributes[attr] += 1
            if attr in REF_NAMES or value.lower().startswith("file:"):
                reference_attributes[attr] += 1
        for child in list(parent):
            relationships[f"{local}/{qname(child.tag)[1]}"] += 1
    return {
        "name": logical_name,
        "root": qname(root.tag)[1],
        "namespaces": dict(sorted(namespaces.items())),
        "elements": dict(sorted(elements.items())),
        "attributes": {key: sorted(value) for key, value in sorted(attributes.items())},
        "time_attributes": dict(sorted(time_attributes.items())),
        "reference_attributes": dict(sorted(reference_attributes.items())),
        "relationships": dict(sorted(relationships.items())),
    }


def script_observation(data, logical_name):
    if len(data) < 16 or data[:4] != b"AACS" or data[4] != 0x11:
        return None
    printable_runs = re.findall(rb"[\x20-\x7e]{4,}", data)
    return {
        "name": logical_name,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "type_byte": 0x11,
        "first_32_bytes_hex": data[:32].hex(),
        "unknown_u16_be_at_5": int.from_bytes(data[5:7], "big"),
        "unknown_u32_be_at_7": int.from_bytes(data[7:11], "big"),
        "printable_run_count": len(printable_runs),
        "printable_run_lengths": sorted((len(value) for value in printable_runs), reverse=True)[:16],
    }


def merge_documents(documents):
    merged = {
        "document_count": len(documents),
        "roots": sorted(Counter(item["root"] for item in documents).items()),
        "namespaces": {},
        "elements": {},
        "attributes": {},
        "time_attributes": {},
        "reference_attributes": {},
        "relationships": {},
    }
    for field in ("namespaces", "elements", "time_attributes", "reference_attributes", "relationships"):
        counts = Counter()
        for item in documents:
            counts.update(item[field])
        merged[field] = dict(sorted(counts.items()))
    attributes = defaultdict(set)
    for item in documents:
        for element, names in item["attributes"].items():
            attributes[element].update(names)
    merged["attributes"] = {key: sorted(value) for key, value in sorted(attributes.items())}
    return merged


def safe_zip_members(archive):
    with zipfile.ZipFile(archive) as source:
        for info in source.infolist():
            path = PurePosixPath(info.filename)
            if info.is_dir() or path.is_absolute() or ".." in path.parts:
                continue
            if path.suffix.lower() in {".xpl", ".aca"}:
                yield str(path), source.read(info)


def directory_members(root):
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix.lower() in {".xpl", ".aca"}:
            yield path.relative_to(root).as_posix(), path.read_bytes()


def library_playlist(xplinfo, path, logical_name):
    """Run hddvd-xplinfo so the report shows how libhddvd models the playlist."""
    process = subprocess.run(
        [str(xplinfo), str(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    try:
        result = json.loads(process.stdout)
    except json.JSONDecodeError:
        result = {"status": "error", "message": process.stderr.strip() or "no output"}
    result["name"] = logical_name
    return result


def inventory(corpus, acaextract, xplinfo=None):
    source_members = safe_zip_members(corpus) if zipfile.is_zipfile(corpus) else directory_members(corpus)
    xpl_documents = []
    library_playlists = []
    xml_by_family = defaultdict(list)
    scripts = []
    archive_count = 0
    with tempfile.TemporaryDirectory(prefix="libhddvd-external-") as temporary:
        temporary = Path(temporary)
        for ordinal, (logical_name, data) in enumerate(source_members):
            suffix = PurePosixPath(logical_name).suffix.lower()
            family = str(PurePosixPath(logical_name).parent)
            if suffix == ".xpl":
                document = analyze_xml(data, logical_name)
                if document:
                    xpl_documents.append(document)
                if xplinfo:
                    playlist = temporary / f"playlist-{ordinal}.xpl"
                    playlist.write_bytes(data)
                    library_playlists.append(library_playlist(xplinfo, playlist, logical_name))
                continue
            archive_count += 1
            archive = temporary / f"archive-{ordinal}.aca"
            output = temporary / f"members-{ordinal}"
            archive.write_bytes(data)
            output.mkdir()
            process = subprocess.run(
                [str(acaextract), str(archive), str(output)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            if process.returncode:
                raise RuntimeError(f"acaextract failed for {logical_name}: {process.stderr.strip()}")
            archive_family = f"{family}/{PurePosixPath(logical_name).stem}"
            for member in sorted(output.iterdir()):
                member_data = member.read_bytes()
                if member.suffix.lower() in {".xmf", ".xmu", ".xss", ".xml"}:
                    document = analyze_xml(member_data, f"{logical_name}/{member.name}")
                    if document:
                        xml_by_family[archive_family].append(document)
                observation = script_observation(member_data, f"{logical_name}/{member.name}")
                if observation:
                    scripts.append(observation)

    xmf_xmu_families = {}
    construct_families = defaultdict(set)
    for family, documents in sorted(xml_by_family.items()):
        xmf_xmu_families[family] = merge_documents(documents)
        for document in documents:
            for element in document["elements"]:
                construct_families[element].add(family)
    recurring = {
        element: sorted(families)
        for element, families in sorted(construct_families.items())
        if len(families) > 1
    }
    unmodeled = Counter()
    for playlist in library_playlists:
        for record in playlist.get("unmodeled", []):
            key = record["element"] + ("@" + record["attribute"] if record["attribute"] else "")
            unmodeled[key] += record["occurrences"]
    library = None
    if xplinfo:
        library = {
            "playlists": library_playlists,
            "failed": sorted(item["name"] for item in library_playlists if item["status"] != "ok"),
            "unmodeled": dict(sorted(unmodeled.items())),
        }
    return {
        "schema": 1,
        "source": {
            "path": str(corpus),
            "sha256": hashlib.sha256(corpus.read_bytes()).hexdigest() if corpus.is_file() else None,
            "policy": "external-only; no source bytes are included in this report",
        },
        "observations": {
            "archive_count": archive_count,
            "xpl": {
                "documents": xpl_documents,
                "aggregate": merge_documents(xpl_documents),
                "library": library,
            },
            "xmf_xmu_families": xmf_xmu_families,
            "constructs_in_multiple_families": recurring,
            "type_0x11_scripts": scripts,
        },
        "inferences": {
            "xpl_parser_candidates": sorted(
                set(merge_documents(xpl_documents)["elements"])
                & {
                    "Configuration", "MediaAttributeList", "TitleSet", "FirstPlayTitle", "Title",
                    "PrimaryAudioVideoClip", "ChapterList", "TrackNavigationList",
                    "ApplicationSegment", "PlaylistApplication", "ScheduledControlList",
                }
            ),
            "script_semantics": "unknown; byte fields and printable runs are structural observations only",
            "aacs_semantics": "unknown; the observed type byte is not treated as an opcode or protection claim",
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("corpus", type=Path, help="external ZIP or extracted directory")
    parser.add_argument("acaextract", type=Path, help="path to hddvd-acaextract")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--xplinfo", type=Path, help="path to hddvd-xplinfo")
    parser.add_argument(
        "--require-modeled",
        action="store_true",
        help="exit 1 unless libhddvd parses every playlist with no unmodeled constructs",
    )
    args = parser.parse_args()
    if not args.corpus.exists():
        parser.error(f"corpus does not exist: {args.corpus}")
    if args.require_modeled and not args.xplinfo:
        parser.error("--require-modeled needs --xplinfo")
    report = inventory(args.corpus, args.acaextract, args.xplinfo)
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    library = report["observations"]["xpl"]["library"]
    if args.require_modeled and (
        not library["playlists"] or library["failed"] or library["unmodeled"]
    ):
        print("inventory: playlists are not fully modeled by libhddvd", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
