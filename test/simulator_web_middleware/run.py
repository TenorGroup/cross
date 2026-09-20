#!/usr/bin/env python3
"""Compile and exercise simulator WebServer middleware through HTTP sockets."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_command(command, log: Path):
    result = subprocess.run(command, capture_output=True, text=True)
    log.write_text(result.stdout + result.stderr)
    return result


def compile_api(cxx: str, source: Path, adapter: Path, output: Path):
    return run_command(
        [cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
         "-I" + str(adapter / "src"),
         "-c", str(source), "-o", str(output / "api-probe.o")],
        output / "api-compile.log",
    )


def compile_runtime(cxx: str, here: Path, adapter: Path, output: Path, baseline: bool):
    binary = output / ("baseline-runtime" if baseline else "middleware-runtime")
    command = [
        cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-pthread",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I" + str(here / "stubs"), "-I" + str(adapter / "src"),
    ]
    if baseline:
        command.append("-DBASELINE")
    command.extend([
        str(here / "harness.cpp"), str(adapter / "src/WebServer.cpp"),
        str(adapter / "src/NetworkClient.cpp"), "-o", str(binary),
    ])
    result = run_command(command, output / ("baseline-compile.log" if baseline else "green-compile.log"))
    return result, binary


def run_binary(binary: Path, log: Path):
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    return run_command([str(binary), str(port)], log)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--phase", choices=("baseline", "green", "patched"), default="green")
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    repo = here.parents[1]
    output = args.output.resolve()
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    adapter = output / "adapter"
    shutil.copytree(args.simulator_root.resolve() / "src", adapter / "src")

    result = {}
    baseline_evidence = True
    if args.phase != "patched":
        baseline_api = compile_api(args.cxx, here / "api_probe.cpp", adapter, output)
        baseline_api_red = (baseline_api.returncode != 0 and
                            "addMiddleware" in (baseline_api.stdout + baseline_api.stderr) and
                            "Middleware" in (baseline_api.stdout + baseline_api.stderr))
        baseline_compile, baseline_binary = compile_runtime(args.cxx, here, adapter, output, True)
        baseline_runtime = None
        if baseline_compile.returncode == 0:
            baseline_runtime = run_binary(baseline_binary, output / "baseline-runtime.log")
        baseline_runtime_red = (
            baseline_runtime is not None and baseline_runtime.returncode != 0 and
            "FAIL middleware called for every completed request" in baseline_runtime.stdout and
            "FAIL authorized status traffic classified separately" in baseline_runtime.stdout
        )
        print(f"{'PASS' if baseline_api_red else 'FAIL'} baseline API is missing middleware support")
        print(f"{'PASS' if baseline_runtime_red else 'FAIL'} baseline socket requests miss middleware callbacks")

        result.update({
            "baseline_api_red": baseline_api_red,
            "baseline_runtime_red": baseline_runtime_red,
            "baseline_compile_exit": baseline_compile.returncode,
            "baseline_run_exit": None if baseline_runtime is None else baseline_runtime.returncode,
        })
        baseline_evidence = baseline_api_red and baseline_runtime_red
        if args.phase == "baseline":
            (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            return 1 if baseline_evidence else 2

    patcher = repo / "scripts/patch_simulator_security.py"
    patch_command = [sys.executable, str(patcher), "--simulator-root", str(adapter), "--middleware-only"]
    first_patch = run_command(patch_command, output / "patch-first.log")
    header = adapter / "src/WebServer.h"
    implementation = adapter / "src/WebServer.cpp"
    first_hashes = (sha256(header), sha256(implementation))
    second_patch = run_command(patch_command, output / "patch-second.log")
    second_hashes = (sha256(header), sha256(implementation))
    idempotent = first_patch.returncode == 0 and second_patch.returncode == 0 and first_hashes == second_hashes

    green_api = compile_api(args.cxx, here / "api_probe.cpp", adapter, output)
    green_compile, green_binary = compile_runtime(args.cxx, here, adapter, output, False)
    green_runtime = None
    if green_compile.returncode == 0:
        green_runtime = run_binary(green_binary, output / "green-runtime.log")
        if green_runtime.stdout or green_runtime.stderr:
            print(green_runtime.stdout + green_runtime.stderr, end="")

    result.update({
        "patch_exit": first_patch.returncode,
        "idempotent": idempotent,
        "green_api_compile_exit": green_api.returncode,
        "green_runtime_compile_exit": green_compile.returncode,
        "green_runtime_exit": None if green_runtime is None else green_runtime.returncode,
        "patched_WebServer_h_sha256": first_hashes[0],
        "patched_WebServer_cpp_sha256": first_hashes[1],
    })
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    green = (baseline_evidence and idempotent and green_api.returncode == 0 and
             green_compile.returncode == 0 and green_runtime is not None and green_runtime.returncode == 0)
    return 0 if green else 1


if __name__ == "__main__":
    raise SystemExit(main())
