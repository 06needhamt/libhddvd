#!/usr/bin/env python3
"""Replay an interaction trace recorded on a player against hddvd-menurun.

The trace (tests/reference_trace.schema.json) lists menu states and the
transitions between them. For every transition this runs the application from
its start along the shortest recorded path to the transition's source state,
performs the transition's event, and compares what libhddvd shows with the
recorded destination: the focused element, the visible elements, and the
player commands the event caused. Screenshot hashes are not compared here:
libhddvd draws nothing itself.

Exit status: 0 when every replayed transition matches, 1 on a mismatch or a
transition that cannot be replayed, 2 for a trace that does not follow the
schema, 77 when the environment names no trace (reference mode).
"""

import argparse
from collections import deque
import json
import os
from pathlib import Path
import re
import subprocess
import sys

KEYS = {"up": "up", "down": "down", "left": "left", "right": "right", "activate": "enter"}
EVENTS = ["open", "close", *KEYS, "timer", "title_end"]


class TraceError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise TraceError(message)


def check_keys(value, where, required, optional=()):
    require(isinstance(value, dict), f"{where} is not an object")
    for key in required:
        require(key in value, f"{where} lacks {key!r}")
    for key in value:
        require(key in required or key in optional, f"{where} has unknown member {key!r}")


def validate(trace):
    """The schema's rules, plus the references a JSON schema cannot state."""
    check_keys(trace, "trace", ["schema", "player", "states", "transitions"])
    require(trace["schema"] == 1, "schema is not 1")
    check_keys(trace["player"], "player", ["name", "version"])
    for key in ("name", "version"):
        require(isinstance(trace["player"][key], str) and trace["player"][key],
                f"player {key} is not a non-empty string")
    require(isinstance(trace["states"], list), "states is not an array")
    states = {}
    for index, state in enumerate(trace["states"]):
        where = f"state {index}"
        check_keys(state, where,
                   ["id", "focused_element", "visible_elements", "screenshot_sha256"],
                   ["title", "position_90khz"])
        require(isinstance(state["id"], str) and state["id"], f"{where} id is empty")
        require(state["id"] not in states, f"{where} repeats id {state['id']!r}")
        require(state["focused_element"] is None or isinstance(state["focused_element"], str),
                f"{where} focused_element is not a string or null")
        require(isinstance(state["visible_elements"], list)
                and all(isinstance(item, str) for item in state["visible_elements"]),
                f"{where} visible_elements is not an array of strings")
        require(isinstance(state["screenshot_sha256"], str)
                and re.fullmatch(r"[0-9a-f]{64}", state["screenshot_sha256"]),
                f"{where} screenshot_sha256 is not a SHA-256")
        for key in ("title", "position_90khz"):
            value = state.get(key)
            require(value is None or (isinstance(value, int) and not isinstance(value, bool)
                                      and value >= 0),
                    f"{where} {key} is not a non-negative integer or null")
        states[state["id"]] = state
    require(isinstance(trace["transitions"], list), "transitions is not an array")
    opens = 0
    for index, transition in enumerate(trace["transitions"]):
        where = f"transition {index}"
        check_keys(transition, where, ["from", "event", "to", "host_commands"], ["wait_ms"])
        require(transition["event"] in EVENTS, f"{where} has unknown event")
        require(isinstance(transition["host_commands"], list)
                and all(isinstance(item, str) for item in transition["host_commands"]),
                f"{where} host_commands is not an array of strings")
        wait = transition.get("wait_ms")
        require(wait is None or (isinstance(wait, int) and not isinstance(wait, bool)
                                 and 0 <= wait <= 86_400_000),
                f"{where} wait_ms is not a number of milliseconds")
        require(wait is None or transition["event"] == "timer",
                f"{where} has wait_ms but is not a timer event")
        if transition["event"] == "open":
            opens += 1
        else:
            require(transition["from"] in states, f"{where} leaves an unknown state")
        if transition["event"] != "close":
            require(transition["to"] in states, f"{where} reaches an unknown state")
    require(opens == 1, "the trace needs exactly one open transition")
    return states


def step_for(transition):
    """The hddvd-menurun step that performs a transition's event, or None."""
    event = transition["event"]
    if event in KEYS:
        return KEYS[event]
    if event == "timer":
        return f"wait:{transition.get('wait_ms', 1000) / 1000:g}"
    if event == "title_end":
        return "event:title_end"
    return None


def paths(trace):
    """The shortest list of transitions from open to each state."""
    start = next(t for t in trace["transitions"] if t["event"] == "open")
    found = {start["to"]: [start]}
    queue = deque([start["to"]])
    while queue:
        state = queue.popleft()
        for transition in trace["transitions"]:
            if (transition["from"] == state and transition["event"] not in ("open", "close")
                    and transition["to"] not in found and step_for(transition)):
                found[transition["to"]] = found[state] + [transition]
                queue.append(transition["to"])
    return found


def run(menurun, options, archive, manifest, steps):
    """Runs the application; returns (focus, visible, commands) after the
    markup loaded handler and after each step."""
    command = [str(menurun), "--state", *options, str(archive), manifest, *steps]
    process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True, check=False)
    if process.returncode not in (0, 1) or "markup loaded handler" not in process.stdout:
        raise RuntimeError(f"hddvd-menurun failed: {process.stderr.strip() or process.stdout}")
    results, current = [], None
    for line in process.stdout.splitlines():
        if line == "markup loaded handler" or line.startswith("step "):
            current = {"focus": None, "visible": None, "commands": []}
            results.append(current)
        elif current is None:
            continue
        elif line.startswith("  focus: "):
            name = line[len("  focus: "):].rsplit(" (generation", 1)[0]
            current["focus"] = None if name == "none" else name
        elif line.startswith("  visible:"):
            current["visible"] = line[len("  visible:"):].split()
        elif line.startswith("  command: "):
            current["commands"].append(line[len("  command: "):])
    if len(results) != len(steps) + 1:
        raise RuntimeError("hddvd-menurun did not perform every step")
    return results


def compare(result, state, commands):
    problems = []
    if result["focus"] != state["focused_element"]:
        problems.append(f"focus is {result['focus']!r}, recorded {state['focused_element']!r}")
    if result["visible"] is None:
        problems.append("visible elements are unknown")
    elif sorted(result["visible"]) != sorted(state["visible_elements"]):
        shown, recorded = set(result["visible"]), set(state["visible_elements"])
        problems.append(f"visible elements differ: extra {sorted(shown - recorded)}, "
                        f"missing {sorted(recorded - shown)}")
    if result["commands"] != commands:
        problems.append(f"commands are {result['commands']}, recorded {commands}")
    return problems


def replay(trace, states, menurun, options, archive, manifest):
    reach = paths(trace)
    report = {"player": trace["player"], "transitions": [], "matched": 0, "mismatched": 0,
              "not_replayed": 0, "screenshots": "not compared"}
    for index, transition in enumerate(trace["transitions"]):
        entry = {"index": index, "from": transition["from"], "event": transition["event"],
                 "to": transition["to"]}
        report["transitions"].append(entry)
        if transition["event"] == "open":
            path, steps = [], []
        elif step_for(transition) is None:
            entry["status"] = "not replayed"
            entry["problems"] = [f"event {transition['event']!r} has no hddvd-menurun step"]
            report["not_replayed"] += 1
            continue
        elif transition["from"] not in reach:
            entry["status"] = "not replayed"
            entry["problems"] = ["no recorded path reaches the source state"]
            report["not_replayed"] += 1
            continue
        else:
            path = reach[transition["from"]][1:] + [transition]
            steps = [step_for(item) for item in path]
        results = run(menurun, options, archive, manifest, steps)
        problems = compare(results[-1], states[transition["to"]], transition["host_commands"])
        entry["steps"] = steps
        entry["status"] = "mismatch" if problems else "match"
        if problems:
            entry["problems"] = problems
        report["mismatched" if problems else "matched"] += 1
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("menurun", type=Path, help="path to hddvd-menurun")
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--manifest")
    parser.add_argument("--playlist", type=Path)
    parser.add_argument("--title")
    parser.add_argument("--report", type=Path, help="write the JSON report here")
    parser.add_argument("--reference", action="store_true",
                        help="take the trace and application from HDDVD_REFERENCE_TRACE, "
                             "HDDVD_REFERENCE_ACA, HDDVD_REFERENCE_MANIFEST and, optionally, "
                             "HDDVD_REFERENCE_PLAYLIST and HDDVD_REFERENCE_TITLE")
    args = parser.parse_args()
    if args.reference:
        env = os.environ
        if not env.get("HDDVD_REFERENCE_TRACE") or not env.get("HDDVD_REFERENCE_ACA"):
            print("reference-trace: HDDVD_REFERENCE_TRACE or HDDVD_REFERENCE_ACA is not set; "
                  "skipping")
            return 77
        args.trace = Path(env["HDDVD_REFERENCE_TRACE"])
        args.archive = Path(env["HDDVD_REFERENCE_ACA"])
        args.manifest = env.get("HDDVD_REFERENCE_MANIFEST")
        args.playlist = Path(env["HDDVD_REFERENCE_PLAYLIST"]) if env.get(
            "HDDVD_REFERENCE_PLAYLIST") else None
        args.title = env.get("HDDVD_REFERENCE_TITLE")
        args.report = Path(env["HDDVD_REFERENCE_TRACE_REPORT"]) if env.get(
            "HDDVD_REFERENCE_TRACE_REPORT") else args.report
    if not args.trace or not args.archive or not args.manifest:
        parser.error("a trace, an archive and a manifest name are needed")
    try:
        trace = json.loads(args.trace.read_text(encoding="utf-8"))
        states = validate(trace)
    except (OSError, ValueError, TraceError) as error:
        print(f"trace: {error}", file=sys.stderr)
        return 2
    options = []
    if args.playlist:
        options += ["--playlist", str(args.playlist)]
        if args.title:
            options += ["--title", args.title]
    try:
        report = replay(trace, states, args.menurun, options, args.archive, args.manifest)
    except RuntimeError as error:
        print(f"replay: {error}", file=sys.stderr)
        return 1
    rendered = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.write_text(rendered, encoding="utf-8")
    for entry in report["transitions"]:
        print(f"{entry['index']}: {entry['from'] or '-'} --{entry['event']}--> {entry['to']}: "
              f"{entry['status']}")
        for problem in entry.get("problems", []):
            print(f"   {problem}")
    print(f"matched {report['matched']}, mismatched {report['mismatched']}, "
          f"not replayed {report['not_replayed']}; screenshots not compared")
    return 1 if report["mismatched"] or report["not_replayed"] else 0


if __name__ == "__main__":
    sys.exit(main())
