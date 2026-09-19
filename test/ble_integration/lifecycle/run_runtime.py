#!/usr/bin/env python3
"""Execute the production BLE runtime and main power path with controlled tasks."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
SCENARIOS = [
    "queued_start_cancelled",
    "queued_idle_stop_keeps_reason",
    "suspend_during_sdk_begin",
    "filetransfer_during_sdk_begin",
    "power_through_pending_teardown",
    "sync_begin_power",
    "double_start_owns_one_worker",
    "main_power_owns_queued_init_and_teardown",
    "worker_creation_failure_releases_owner",
    "sdk_begin_failure_releases_power",
    "postinit_headroom_pending_teardown",
]
SOURCE_FILES = [
    "src/BlePageTurnerRuntime.cpp",
    "src/BlePageTurnerRuntime.h",
    "src/FileTransferState.h",
    "src/main.cpp",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = output / "runtime-test"

    # Keep the production branch unchanged, including its conditional compile
    # guard and delays. Only the surrounding hardware boundaries are fakes.
    main_source = (source / "src/main.cpp").read_text()
    tail_start = main_source.index("  if (activityManager.skipLoopDelay()) {")
    tail_end = main_source.index("\n}\n", tail_start)
    (output / "main_power.inc").write_text(main_source[tail_start:tail_end] + "\n")
    command = [
        args.compiler, "-std=c++17", "-Wall", "-Wextra",
        "-DFREEINK_CAP_BLE_HID_HOST=1", "-DESP_PLATFORM=1",
        "-DCROSSPOINT_BLE_HID_HOST=1", "-I" + str(output),
        "-I" + str(HERE / "stubs"), "-I" + str(source / "src"),
        str(HERE / "runtime_test.cpp"),
        str(source / "src/BlePageTurnerRuntime.cpp"), "-o", str(binary),
    ]
    build = subprocess.run(command, cwd=output, text=True, capture_output=True)
    (output / "build.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        print(build.stdout + build.stderr, end="")
        return build.returncode

    results = []
    for scenario in SCENARIOS:
        run = subprocess.run([str(binary), scenario], cwd=output, text=True, capture_output=True)
        result = run.stdout + run.stderr
        print(result, end="")
        results.append({"scenario": scenario, "exit_code": run.returncode, "output": result})
    (output / "run.log").write_text("".join(item["output"] for item in results))
    manifest = {
        "source": str(source),
        "sources_sha256": {
            name: hashlib.sha256((source / name).read_bytes()).hexdigest()
            for name in SOURCE_FILES
        },
        "compile_command": command,
        "tests": results,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return int(any(item["exit_code"] for item in results))


if __name__ == "__main__":
    sys.exit(main())
