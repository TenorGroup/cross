#!/usr/bin/env python3
"""Compile production ReadingStatsStore with reset finalization fault injection."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--source-root", type=pathlib.Path)
parser.add_argument("--output", type=pathlib.Path, required=True)
parser.add_argument("--arduinojson", type=pathlib.Path, required=True)
args = parser.parse_args()
repo = pathlib.Path(__file__).resolve().parents[2]
source = args.source_root or repo
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)

for name in ("ReadingStatsStore.cpp", "ReadingStatsStore.h"):
    shutil.copy2(source / "src" / name, output / name)

inputs = [
    source / "src" / "ReadingStatsStore.cpp",
    source / "src" / "ReadingStatsStore.h",
    repo / "test" / "history_navigation" / "reset_finalization.cpp",
    repo / "test" / "history_navigation" / "stubs" / "HalStorage.h",
]
(output / "source-hashes.json").write_text(json.dumps({
    str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs
}, indent=2))

command = [
    "c++", "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    "-DARDUINOJSON_ENABLE_ARDUINO_STRING=0",
    "-I" + str(repo / "test" / "history_navigation" / "stubs"),
    "-I" + str(output),
    "-I" + str(repo / "src"),
    "-I" + str(args.arduinojson.resolve()),
    str(repo / "test" / "history_navigation" / "reset_finalization.cpp"),
    *[str(repo / "src" / "util" / name) for name in ("ReadingHabits.cpp", "SoLieuDoc.cpp", "NgayGio.cpp")],
    "-o", str(output / "ReadingStatsResetFinalizationTest"),
]
(output / "compile-command.json").write_text(json.dumps(command, indent=2))
compiled = subprocess.run(command, capture_output=True, text=True)
(output / "compile.log").write_text(compiled.stdout + compiled.stderr)
if compiled.returncode:
    print(compiled.stdout + compiled.stderr, end="")
    raise SystemExit(compiled.returncode)

result = subprocess.run([str(output / "ReadingStatsResetFinalizationTest")], capture_output=True, text=True)
(output / "results.log").write_text(result.stdout + result.stderr)
print(result.stdout + result.stderr, end="")
raise SystemExit(result.returncode)
