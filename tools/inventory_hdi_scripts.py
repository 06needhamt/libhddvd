#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Tom Needham
"""List the host objects, methods and events that HDi ECMAScript files use.

Each argument is a source: a .js file or a directory searched recursively for
.js and .xmu files (for example one disc's extracted archive members). Markup
is read only for the event names its timing declares. Files may be bare
source or wrapped in the clear AACS envelope (type 0x12); protected envelopes
are counted and skipped. Text may be UTF-16 with a byte-order mark or UTF-8.

A host global is an identifier that a script reads but no script in any source
declares or assigns, and that is not an ECMAScript built-in. Each use is
reported as a member chain, with calls written as () and indexing as [], for
example Player.playlist.titles[].jump(). Methods called on other objects whose
names no script defines are listed separately; those are a heuristic for host
methods reached through variables (event objects, timers, elements).

The report contains identifiers, event names and counts only, never source
text, so it can be shared without copying the scripts.
"""

import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re
import sys

AACS_HEADER = 283
MARKUP_EVENT = re.compile(r"<event\b[^>]*?\bname\s*=\s*[\"']([^\"']+)")
KEYWORDS = {
    "break", "case", "catch", "continue", "default", "delete", "do", "else", "finally", "for",
    "function", "if", "in", "instanceof", "new", "return", "switch", "this", "throw", "try",
    "typeof", "var", "void", "while", "with", "true", "false", "null", "const", "let",
}
BUILTINS = {
    "Object", "Function", "Array", "String", "Boolean", "Number", "Math", "Date", "RegExp",
    "Error", "EvalError", "RangeError", "ReferenceError", "SyntaxError", "TypeError",
    "URIError", "NaN", "Infinity", "undefined", "eval", "parseInt", "parseFloat", "isNaN",
    "isFinite", "decodeURI", "decodeURIComponent", "encodeURI", "encodeURIComponent",
    "escape", "unescape", "arguments",
}
BUILTIN_METHODS = {
    # Object.prototype, Function.prototype
    "toString", "toLocaleString", "valueOf", "hasOwnProperty", "isPrototypeOf",
    "propertyIsEnumerable", "apply", "call",
    # Array.prototype
    "concat", "join", "pop", "push", "reverse", "shift", "slice", "sort", "splice", "unshift",
    # String.prototype
    "charAt", "charCodeAt", "indexOf", "lastIndexOf", "localeCompare", "match", "replace",
    "search", "split", "substring", "substr", "toLowerCase", "toUpperCase",
    "toLocaleLowerCase", "toLocaleUpperCase", "fromCharCode",
    # Number.prototype
    "toFixed", "toExponential", "toPrecision",
    # RegExp.prototype
    "exec", "test",
    # Math and Date
    "abs", "acos", "asin", "atan", "atan2", "ceil", "cos", "exp", "floor", "log", "max", "min",
    "pow", "random", "round", "sin", "sqrt", "tan", "getTime", "getFullYear", "getMonth",
    "getDate", "getDay", "getHours", "getMinutes", "getSeconds", "getMilliseconds",
    "getTimezoneOffset", "setTime", "setFullYear", "setMonth", "setDate", "setHours",
    "setMinutes", "setSeconds", "setMilliseconds", "getUTCFullYear", "getUTCMonth",
    "getUTCDate", "getUTCDay", "getUTCHours", "getUTCMinutes", "getUTCSeconds", "getYear",
    "toDateString", "toTimeString", "toUTCString", "toGMTString", "UTC", "parse",
}
PUNCTUATORS = sorted(
    "{ } ( ) [ ] . ; , < > <= >= == != === !== + - * % ++ -- << >> >>> & | ^ ! ~ && || ? : = "
    "+= -= *= %= <<= >>= >>>= &= |= ^= / /=".split(),
    key=len,
    reverse=True,
)
# After these tokens a slash starts a regular expression rather than a division.
REGEX_AFTER = set("( , = : [ ! & | ? { } ; + - * % < > ~ ^".split()) | {
    "return", "typeof", "in", "new", "delete", "void", "case", "throw", "else", "do",
    "&&", "||", "==", "!=", "===", "!==", "<=", ">=", "+=", "-=", "*=", "/=", "%=",
}


def payload(data):
    """Returns (kind, bytes): kind is "readable", "protected" or "unrecognized"."""
    if data[:4] == b"AACS":
        if len(data) < AACS_HEADER or data[4] != 0x12:
            return "protected", None
        length = int.from_bytes(data[7:11], "big")
        if AACS_HEADER + length > len(data):
            return "unrecognized", None
        return "readable", data[AACS_HEADER : AACS_HEADER + length]
    return "readable", data


def decode(data):
    if data[:2] == b"\xfe\xff":
        return data[2:].decode("utf-16-be")
    if data[:2] == b"\xff\xfe":
        return data[2:].decode("utf-16-le")
    return data.decode("utf-8-sig")


def tokenize(text):
    """Yields (kind, value) with kind "id", "str", "num", "punct" or "regex"."""
    i, n = 0, len(text)
    previous = None
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
        elif c in "\"'":
            j = i + 1
            value = []
            while j < n and text[j] != c and text[j] != "\n":
                if text[j] == "\\" and j + 1 < n:
                    value.append(text[j + 1])
                    j += 2
                else:
                    value.append(text[j])
                    j += 1
            previous = ("str", "".join(value))
            yield previous
            i = j + 1
        elif c.isalpha() or c in "_$":
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] in "_$"):
                j += 1
            previous = ("id", text[i:j])
            yield previous
            i = j
        elif c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit()):
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] == "."):
                j += 1
            previous = ("num", text[i:j])
            yield previous
            i = j
        elif c == "/" and (previous is None or previous[1] in REGEX_AFTER):
            j = i + 1
            in_class = False
            while j < n and text[j] != "\n":
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == "[":
                    in_class = True
                elif text[j] == "]":
                    in_class = False
                elif text[j] == "/" and not in_class:
                    break
                j += 1
            j += 1
            while j < n and text[j].isalnum():
                j += 1
            previous = ("regex", "")
            yield previous
            i = j
        else:
            for p in PUNCTUATORS:
                if text.startswith(p, i):
                    previous = ("punct", p)
                    yield previous
                    i += len(p)
                    break
            else:
                i += 1


def skip_group(tokens, i):
    """Given tokens[i] opening a bracket, returns the index after its match."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    stack = [pairs[tokens[i][1]]]
    i += 1
    while i < len(tokens) and stack:
        value = tokens[i][1] if tokens[i][0] == "punct" else None
        if value in pairs:
            stack.append(pairs[value])
        elif value == stack[-1]:
            stack.pop()
        i += 1
    return i


def is_punct(tokens, i, value):
    return 0 <= i < len(tokens) and tokens[i] == ("punct", value)


def declarations(tokens):
    """Names the scripts define: functions, variables, parameters, catch
    variables, assigned globals, and members they create or assign."""
    names, members = set(), set()
    for i, (kind, value) in enumerate(tokens):
        if kind != "id":
            continue
        after_dot = is_punct(tokens, i - 1, ".")
        if value == "function":
            j = i + 1
            if j < len(tokens) and tokens[j][0] == "id":
                names.add(tokens[j][1])
                j += 1
            if is_punct(tokens, j, "("):
                end = skip_group(tokens, j)
                names.update(v for k, v in tokens[j:end] if k == "id")
        elif value == "catch" and is_punct(tokens, i + 1, "("):
            if i + 2 < len(tokens) and tokens[i + 2][0] == "id":
                names.add(tokens[i + 2][1])
        elif value == "var":
            # Every name in the list, skipping initializers up to the next
            # top-level comma.
            j = i + 1
            while j < len(tokens) and tokens[j][0] == "id":
                names.add(tokens[j][1])
                j += 1
                while j < len(tokens) and not (
                    is_punct(tokens, j, ",")
                    or is_punct(tokens, j, ";")
                    or tokens[j] in (("id", "in"), ("id", "var"))
                ):
                    if tokens[j][0] == "punct" and tokens[j][1] in "([{":
                        j = skip_group(tokens, j)
                    elif is_punct(tokens, j, ")") or is_punct(tokens, j, "}"):
                        break
                    else:
                        j += 1
                if not is_punct(tokens, j, ","):
                    break
                j += 1
        elif is_punct(tokens, i - 1, ",") or is_punct(tokens, i - 1, "{"):
            if is_punct(tokens, i + 1, ":"):
                members.add(value)  # object literal key
        if is_punct(tokens, i + 1, "=") and value not in KEYWORDS:
            (members if after_dot else names).add(value)
    return names, members


def chain_at(tokens, i):
    """Reads a member chain starting at identifier tokens[i]; returns
    (parts, first argument token of the final call or None)."""
    parts = [tokens[i][1]]
    argument = None
    i += 1
    while i < len(tokens):
        if is_punct(tokens, i, ".") and i + 1 < len(tokens) and tokens[i + 1][0] == "id":
            parts.append(tokens[i + 1][1])
            i += 2
        elif is_punct(tokens, i, "["):
            parts[-1] += "[]"
            i = skip_group(tokens, i)
        elif is_punct(tokens, i, "("):
            argument = tokens[i + 1] if i + 1 < len(tokens) else None
            parts[-1] += "()"
            i = skip_group(tokens, i)
        else:
            break
    return parts, argument


class Inventory:
    def __init__(self):
        self.files = Counter()
        self.chains = defaultdict(Counter)  # chain -> source -> uses
        self.events = defaultdict(Counter)
        self.created = set()  # event types the scripts create
        self.markup_events = set()  # event names markup timing declares
        self.methods = defaultdict(Counter)
        self.programs = []  # (source, tokens)

    def add(self, source, path):
        kind, data = payload(path.read_bytes())
        if kind == "readable":
            try:
                text = decode(data)
            except UnicodeDecodeError:
                kind = "unrecognized"
        if path.suffix.lower() == ".xmu":
            # Markup timing can fire named events; only those names are kept.
            if kind == "readable":
                self.markup_events.update(MARKUP_EVENT.findall(text))
            return
        self.files[(source, kind)] += 1
        if kind == "readable":
            self.programs.append((source, list(tokenize(text))))

    def analyze(self):
        names, members = set(), set()
        for _, tokens in self.programs:
            found_names, found_members = declarations(tokens)
            names |= found_names
            members |= found_members
        defined = names | members
        for source, tokens in self.programs:
            i = 0
            while i < len(tokens):
                kind, value = tokens[i]
                if kind != "id" or value in KEYWORDS or is_punct(tokens, i - 1, "."):
                    i += 1
                    continue
                key = is_punct(tokens, i + 1, ":") and (
                    is_punct(tokens, i - 1, "{") or is_punct(tokens, i - 1, ",")
                )
                if key or (i > 0 and tokens[i - 1] == ("id", "function")):
                    i += 1
                    continue
                parts, argument = chain_at(tokens, i)
                host = value not in names and value not in BUILTINS
                if host:
                    self.chains[".".join(parts)][source] += 1
                for part in parts[1:]:
                    if part.endswith("()"):
                        method = part.split("(")[0].rstrip("[]")
                        if method not in defined and method not in BUILTIN_METHODS and not host:
                            self.methods[method][source] += 1
                call = parts[-1].split("(")[0] if parts[-1].endswith("()") else None
                if argument and argument[0] == "str":
                    if call in ("addEventListener", "removeEventListener"):
                        self.events[argument[1]][source] += 1
                    elif call in ("createEvent", "initEvent") or (
                        len(parts) == 1 and value in names
                    ):
                        # Created directly, or handed to a script helper
                        # that creates it.
                        self.created.add(argument[1])
                # Arguments are scanned too, so only step past the name.
                i += 1

    def origin(self, event):
        """Where an event comes from: a script, markup timing, or "other" when
        neither raises it (the player, or a source that was not scanned)."""
        if event in self.created:
            return "script"
        return "markup" if event in self.markup_events else "other"

    def report(self):
        sources = sorted({source for source, _ in self.files})
        lines = []
        for source in sources:
            lines.append(
                "source %s readable=%d protected=%d unrecognized=%d"
                % (
                    source,
                    self.files[(source, "readable")],
                    self.files[(source, "protected")],
                    self.files[(source, "unrecognized")],
                )
            )

        def users(counter):
            return "uses=%d sources=%s" % (sum(counter.values()), ",".join(sorted(counter)))

        globals_ = defaultdict(Counter)
        for chain, counter in self.chains.items():
            globals_[chain.split(".")[0].split("(")[0].split("[")[0]].update(counter)
        for name in sorted(globals_):
            lines.append("global %s %s" % (name, users(globals_[name])))
            for chain in sorted(c for c in self.chains if c.split(".")[0].split("(")[0].split("[")[0] == name):
                lines.append("  chain %s %s" % (chain, users(self.chains[chain])))
        for event in sorted(self.events):
            lines.append(
                "event %s %s origin=%s" % (event, users(self.events[event]), self.origin(event))
            )
        for method in sorted(self.methods):
            lines.append("method %s %s" % (method, users(self.methods[method])))
        lines.append(
            "summary sources=%d readable=%d protected=%d unrecognized=%d globals=%d chains=%d "
            "events=%d other-events=%d methods=%d"
            % (
                len(sources),
                sum(v for (_, k), v in self.files.items() if k == "readable"),
                sum(v for (_, k), v in self.files.items() if k == "protected"),
                sum(v for (_, k), v in self.files.items() if k == "unrecognized"),
                len(globals_),
                len(self.chains),
                len(self.events),
                len([e for e in self.events if self.origin(e) == "other"]),
                len(self.methods),
            )
        )
        return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("sources", nargs="+", type=Path, help=".js files or directories")
    args = parser.parse_args()
    inventory = Inventory()
    for source in args.sources:
        if source.is_dir():
            files = sorted(p for p in source.rglob("*") if p.suffix.lower() in (".js", ".xmu"))
        elif source.is_file():
            files = [source]
        else:
            parser.error("%s does not exist" % source)
        for path in files:
            inventory.add(source.name, path)
    inventory.analyze()
    sys.stdout.write(inventory.report())
    return 0


if __name__ == "__main__":
    sys.exit(main())
