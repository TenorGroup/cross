#!/usr/bin/env python3
"""Compile and run WifiSelectionActivity production behavior with fake hardware."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    source_root = (args.source_root or here.parents[1]).resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = output / "wifi-power-behavior"
    harness = here / "WifiPowerBehavior.cpp"
    stub_files = sorted((here / "stubs").rglob("*.h"))
    cpp = source_root / "src/activities/network/WifiSelectionActivity.cpp"
    header = source_root / "src/activities/network/WifiSelectionActivity.h"

    command = [
        args.cxx,
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-switch",
        "-Wno-unused-parameter",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I" + str(here / "stubs"),
        "-I" + str(source_root / "src/activities/network"),
        "-I" + str(source_root / "src"),
        str(harness),
        "-o",
        str(binary),
    ]
    (output / "compile-command.json").write_text(json.dumps(command, indent=2) + "\n")
    compile_result = subprocess.run(command, capture_output=True, text=True)
    (output / "compile.log").write_text(compile_result.stdout + compile_result.stderr)

    source_hashes = {
        "source_root": str(source_root),
        "WifiSelectionActivity.cpp": sha256(cpp),
        "WifiSelectionActivity.h": sha256(header),
        "WifiPowerBehavior.cpp": sha256(harness),
        "run.py": sha256(here / "run.py"),
        "stubs": {str(path.relative_to(here)): sha256(path) for path in stub_files},
    }
    (output / "source-hashes.json").write_text(json.dumps(source_hashes, indent=2) + "\n")

    if compile_result.returncode:
        print(compile_result.stdout + compile_result.stderr, end="")
        result = {"compile_exit": compile_result.returncode, "run_exit": None, "passed": 0, "failed": None}
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        return compile_result.returncode

    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "halt_on_error=1"
    run_result = subprocess.run([str(binary)], capture_output=True, text=True, env=environment)
    log = run_result.stdout + run_result.stderr
    print(log, end="")
    (output / "result.log").write_text(log)
    passed = sum(line.startswith("PASS ") for line in log.splitlines())
    failed = sum(line.startswith("FAIL ") for line in log.splitlines())
    result = {
        "compile_exit": 0,
        "run_exit": run_result.returncode,
        "passed": passed,
        "failed": failed,
        "binary_sha256": sha256(binary),
    }
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return run_result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
