#!/usr/bin/env python3
"""Build the SDK's IMU driver and the tilt sensor HAL on a fake QMI8658 and run them.

Default: the checks in harness.cpp against this tree, with double tap Off compared with the
v1.0.16 trace (imu-trace-v1016.txt) and the 26/09 knock run replayed at 224 Hz.

--record <tree>: prints the Off scenario's transactions for another tree holding
lib/hal/HalTiltSensor.{h,cpp} and freeink-sdk/libs/hardware/Imu. imu-trace-v1016.txt came
from release/v1.0.16 (HalTiltSensor at 87fb0720, freeink-sdk efe8e40) this way.
"""

import argparse
import gzip
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, help="the tree to check")
parser.add_argument("--record", type=Path, help="print the Off trace of this tree instead")
args = parser.parse_args()
if not args.source and not args.record:
    parser.error("--source or --record is required")

test_root = Path(__file__).resolve().parent
tree = (args.record or args.source).resolve()
hal = tree / "lib/hal/HalTiltSensor.h"
flags = []
if not args.record:
    header = hal.read_text()
    flags += ["-DHAVE_FLIP"] if "configureFlip" in header else []
    flags += ["-DHAVE_DOUBLE_TAP"] if "configureDoubleTap" in header else []
else:
    flags.append("-DGOLDEN_ONLY")

compiler = shutil.which("clang++") or shutil.which("c++")
if not compiler:
    raise RuntimeError("missing C++ compiler")

with tempfile.TemporaryDirectory(prefix="imu-double-tap-") as temporary_dir:
    output = Path(temporary_dir)
    executable = output / "imu-double-tap"
    command = [
        compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Wno-unused-private-field", *flags,
        "-I", str(test_root / "stubs"),
        "-I", str(tree / "lib/hal"),
        "-I", str(tree / "freeink-sdk/libs/hardware/Imu/include"),
        str(test_root / "harness.cpp"),
        str(tree / "lib/hal/HalTiltSensor.cpp"),
        str(tree / "freeink-sdk/libs/hardware/Imu/src/Imu.cpp"),
        "-o", str(executable),
    ]
    build = subprocess.run(command, text=True, capture_output=True)
    if build.stdout:
        print(build.stdout, end="")
    if build.stderr:
        print(build.stderr, end="", file=sys.stderr)
    if build.returncode:
        sys.exit(build.returncode)
    if args.record:
        sys.exit(subprocess.run([str(executable)]).returncode)

    runs = []
    for name in ("tap-224hz.csv", "tap-224hz-canh.csv", "tap-224hz-canh2.csv", "tap-224hz-manhinh.csv", "tap-fifo.csv", "tap-cuoi.csv"):
        runs.append(output / name)
        runs[-1].write_bytes(gzip.decompress((test_root / (name + ".gz")).read_bytes()))
    run = subprocess.run([str(executable), str(test_root / "imu-trace-v1016.txt"), *map(str, runs)], text=True,
                         capture_output=True)
    if run.stdout:
        print(run.stdout, end="")
    if run.stderr:
        print(run.stderr, end="", file=sys.stderr)
    sys.exit(run.returncode)
