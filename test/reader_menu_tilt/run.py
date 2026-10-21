#!/usr/bin/env python3
"""Tilt in the reader's menu: the production sensor, the production main-loop route and screen note, and the
production shared tab poll, against a model of the reader around them. The reader's own text is compiled in
too: the body of managesTiltSensor and the tilt block at the head of handleOverlayInput, so the test goes red on
a reader whose menu does not hand the sensor over or does not poll it as the harness expects."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def extract_function(source: str, expression: str) -> str:
    match = re.search(expression, source)
    if not match:
        raise ValueError("missing " + expression)
    brace = source.find("{", match.end())
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1]
    raise ValueError("unclosed " + expression)


def tilt_block(source: str) -> str:
    """The head of handleOverlayInput: `menutilt::Step tilt;` through the if/else that fills it. A reader without
    the block does nothing with tilt in its menu, which is what an empty block says."""
    method = extract_function(source, r"void EpubReaderActivity::handleOverlayInput\s*\(\)")
    start = method.find("menutilt::Step tilt;")
    if start < 0:
        return "menutilt::Step tilt;"

    def close(text: str, at: int) -> int:
        at = text.index("{", at)
        depth = 0
        for index in range(at, len(text)):
            depth += (text[index] == "{") - (text[index] == "}")
            if depth == 0:
                return index + 1
        raise ValueError("unclosed block")

    end = close(method, start)
    if method[end:].lstrip().startswith("else"):
        end = close(method, end)
    return method[start:end]


def manages_tilt_sensor(header: str, activity_header: str) -> str:
    """The reader's own override, or the Activity default it inherits when the reader has none."""
    own = re.search(r"bool managesTiltSensor\(\) const override \{[^}]*\}", header)
    if own:
        return own.group(0).replace(" override", "")
    return re.search(r"bool managesTiltSensor\(\) const \{[^}]*\}", activity_header).group(0)


parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True, type=Path)
args = parser.parse_args()
root = args.source.resolve()
test_root = Path(__file__).resolve().parent

main_source = (root / "src/main.cpp").read_text()
reader_header = (root / "src/activities/reader/EpubReaderActivity.h").read_text()
reader_source = (root / "src/activities/reader/EpubReaderActivity.cpp").read_text()
activity_header = (root / "src/activities/Activity.h").read_text()

route = extract_function(main_source, r"static void updateTiltSensorForForegroundActivity\s*\(")
note = re.search(r"halTiltSensor\.noteScreen\([^;]*\);", main_source)
if not note:
    raise ValueError("main does not note the screen in front")
note_screen = note.group(0).replace("activityManager.activityGeneration()", "generation")

compiler = shutil.which("clang++") or shutil.which("c++")
if not compiler:
    raise RuntimeError("missing C++ compiler")

with tempfile.TemporaryDirectory(prefix="reader-menu-tilt-") as temporary_dir:
    out = Path(temporary_dir)
    harness = (
        (test_root / "harness.cpp.in")
        .read_text()
        .replace("@MAIN_TILT_ROUTE@", route)
        .replace("@NOTE_SCREEN@", note_screen)
        .replace("@MENU_MANAGES@", manages_tilt_sensor(reader_header, activity_header))
        .replace("@MENU_TILT_BLOCK@", tilt_block(reader_source))
    )
    (out / "harness.cpp").write_text(harness)
    build = subprocess.run(
        [
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-private-field",
            "-I", str(test_root / "stubs"),
            "-I", str(test_root.parent / "tilt_sensor_ownership/stubs"),
            "-I", str(root / "lib/hal"),
            "-I", str(root / "freeink-sdk/libs/hardware/Imu/include"),
            "-I", str(root / "src"),
            str(out / "harness.cpp"), str(root / "lib/hal/HalTiltSensor.cpp"),
            "-o", str(out / "harness"),
        ],
        text=True, capture_output=True,
    )
    sys.stderr.write(build.stderr)
    if build.returncode:
        sys.exit(build.returncode)
    run = subprocess.run([str(out / "harness")], text=True, capture_output=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)
    sys.exit(run.returncode)
