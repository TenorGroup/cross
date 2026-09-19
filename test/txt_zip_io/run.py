#!/usr/bin/env python3
"""Compile unchanged production method bodies against deterministic I/O faults."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def method(source, name):
    match = re.search(r"^[\w:*<> ]+\b" + re.escape(name) + r"\s*\(", source, re.M)
    if not match:
        raise RuntimeError("Missing method: " + name)
    start = match.start()
    brace = source.index("{", match.end())
    depth, state, pos = 1, "code", brace + 1
    while depth:
        c = source[pos]
        nxt = source[pos:pos + 2]
        if state == "line":
            if c == "\n":
                state = "code"
        elif state == "block":
            if nxt == "*/":
                state = "code"
                pos += 1
        elif state in ('"', "'"):
            if c == "\\":
                pos += 1
            elif c == state:
                state = "code"
        elif nxt == "//":
            state = "line"
            pos += 1
        elif nxt == "/*":
            state = "block"
            pos += 1
        elif c in ('"', "'"):
            state = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        pos += 1
    return source[start:pos]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", default="all")
    parser.add_argument("--replay", type=Path, help="Compile a frozen production-slices.cpp with current cases")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    root = args.source.resolve()
    here = Path(__file__).resolve().parent
    args.output.mkdir(parents=True, exist_ok=True)
    source_paths = ["lib/Txt/Txt.cpp", "lib/ZipFile/ZipFile.cpp",
                    "src/activities/reader/TxtReaderActivity.cpp",
                    "src/activities/reader/TxtReaderActivity.h", "lib/Serialization/Serialization.h"]
    sources = {name: (root / name).read_text() for name in source_paths}
    reader = sources[source_paths[2]]
    names = ["initializeReader", "buildPageIndex", "loadPageAtOffset", "loadPageIndexCache", "savePageIndexCache"]
    for optional in ("reservePageOffsets", "addPageOffset", "clearPageOffsets", "loadPageIndexCacheFile"):
        if f"TxtReaderActivity::{optional}(" in reader:
            names.append(optional)
    bodies = [method(reader, "TxtReaderActivity::" + name) for name in names]
    header = sources[source_paths[3]]
    fields = header[header.index("{", header.index("class TxtReaderActivity")) + 1:header.index("  void renderPage")]
    declarations = [body[:body.index("{")].strip().replace("TxtReaderActivity::", "") + ";" for body in bodies]
    declarations = [d.replace("std::vector<uint16_t>* lineY,", "std::vector<uint16_t>* lineY = nullptr,")
                    .replace("std::vector<uint16_t>* lineIndent)", "std::vector<uint16_t>* lineIndent = nullptr)")
                    for d in declarations]
    generated = '#include "Harness.h"\n#include "Serialization.h"\n#include "Memory.h"\n#include "RecoverableFile.h"\n'
    constants = reader[reader.index("namespace {"):reader.index("}  // namespace")]
    generated += constants + "}\n"
    generated += "class TxtReaderActivity { public:\n" + fields + "\n" + "\n".join(declarations)
    generated += "\nbool preview = false; int progressLoads = 0; void loadProgress() { ++progressLoads; }\n"
    generated += "void readingMargins(int& a,int& b,int& c,int& d) { a=b=c=d=0; }\n};\n"
    generated += method(sources[source_paths[0]], "Txt::readContent") + "\n"
    generated += method(sources[source_paths[1]], "ZipFile::readFileToStream") + "\n"
    generated += "\n".join(bodies)
    generated += '\n#include "Cases.cpp"\n'
    cpp = args.output / "production-slices.cpp"
    cpp.write_text(args.replay.read_text() if args.replay else generated)
    (args.output / "provenance.json").write_text(json.dumps({
        "source": str(root),
        "replay": str(args.replay) if args.replay else None,
        "methods": ["Txt::readContent", "ZipFile::readFileToStream"] + ["TxtReaderActivity::" + n for n in names],
        "sha256": {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in source_paths},
    }, indent=2) + "\n")
    executable = args.output / "txt-zip-io"
    flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []
    subprocess.run([args.cxx, "-std=c++20", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Wno-unused-parameter", "-g", *flags,
                    "-I" + str(here), "-I" + str(here / "stubs"), "-I" + str(root / "lib/Serialization"),
                    "-I" + str(root / "lib/Memory"),
                    "-I" + str(root / "freeink-sdk/libs/hardware/SDCardManager/include"),
                    str(cpp), "-o", str(executable)], check=True)
    return subprocess.run([str(executable), args.case]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
