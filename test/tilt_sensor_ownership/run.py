#!/usr/bin/env python3
"""Compile the production IMU state machine against foreground-owner routes."""

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
        raise ValueError("missing production foreground tilt route")
    start = match.start()
    brace = source.find("{", match.end())
    if brace < 0:
        raise ValueError("foreground tilt route has no body")
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise ValueError("foreground tilt route body is unclosed")


parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--legacy", action="store_true", help="run the pre-fix main then tab collision")
args = parser.parse_args()

source_root = args.source.resolve()
test_root = Path(__file__).resolve().parent
main_source = (source_root / "src/main.cpp").read_text()


def require(expression: str, source: str, detail: str) -> None:
    if not re.search(expression, source, re.DOTALL):
        raise ValueError(detail)

if args.legacy:
    old_call = re.compile(
        r"halTiltSensor\.update\(\s*SETTINGS\.tiltPageTurn,\s*SETTINGS\.orientation,\s*"
        r"activityManager\.isForegroundReaderActivity\(\)\s*\);"
    )
    if not old_call.search(main_source):
        raise ValueError("legacy main foreground tilt call is missing")
    main_tilt_route = """static void updateTiltSensorForForegroundActivity(const bool foregroundReader, const bool) {
  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, foregroundReader);
}"""
else:
    require(
        r"const bool foregroundReader\s*=\s*activityManager\.isForegroundReaderActivity\(\);\s*"
        r"const bool foregroundActivityManagesTiltSensor\s*=\s*"
        r"activityManager\.isForegroundActivityManagingTiltSensor\(\);\s*"
        r"updateTiltSensorForForegroundActivity\(foregroundReader, foregroundActivityManagesTiltSensor\);",
        main_source,
        "main loop is not wired to the foreground tilt owner route",
    )
    require(
        r"virtual bool managesTiltSensor\(\) const\s*\{\s*return false;\s*\}",
        (source_root / "src/activities/Activity.h").read_text(),
        "base activity does not declare the tilt ownership boundary",
    )
    require(
        r"bool managesTiltSensor\(\) const override\s*\{\s*return true;\s*\}",
        (source_root / "src/activities/UiTabListActivity.h").read_text(),
        "tab activity does not own its tilt update loop",
    )
    require(
        r"void UiTabListActivity::loop\(\)\s*\{\s*"
        r"const bool acceptsTilt\s*=\s*acceptsTiltTabNavigation\(\);\s*"
        r"const auto orientation\s*=\s*static_cast<CrossPointOrientation::Value>\(renderer\.getOrientation\(\)\);\s*"
        r"halTiltSensor\.update\(SETTINGS\.tiltTabNavigation, static_cast<uint8_t>\(orientation\), acceptsTilt\);",
        (source_root / "src/activities/UiTabListActivity.cpp").read_text(),
        "tab loop no longer supplies its own tilt setting and target gate",
    )
    require(
        r"bool ActivityManager::isForegroundActivityManagingTiltSensor\(\) const\s*\{\s*"
        r"return pendingAction == PendingAction::None && currentActivity && currentActivity->managesTiltSensor\(\);\s*\}",
        (source_root / "src/activities/ActivityManager.cpp").read_text(),
        "activity manager does not exclude pending transitions from the tab owner",
    )
    require(
        r"if \(mappedInputManager\.wasReleased\(MappedInputManager::Button::Power\)\) \{\s*"
        r"runQuickAction\(SETTINGS\.shortPwrBtn, quickaction::Trigger::PowerRelease\);\s*\}\s*"
        r"if \(halTiltSensor\.wasShaken\(\)\) \{\s*"
        r"runQuickAction\(quickaction::shakeAsPowerAction\(SETTINGS\.shakeAction\), quickaction::Trigger::Shake\);",
        main_source,
        "short power press and hard shake do not share the one action route",
    )
    main_tilt_route = extract_function(
        main_source, r"static void updateTiltSensorForForegroundActivity\s*\("
    )

compiler = shutil.which("clang++") or shutil.which("c++")
if not compiler:
    raise RuntimeError("missing C++ compiler")

with tempfile.TemporaryDirectory(prefix="tilt-sensor-ownership-") as temporary_dir:
    output = Path(temporary_dir)
    harness = (test_root / "harness.cpp.in").read_text().replace("@MAIN_TILT_ROUTE@", main_tilt_route)
    generated = output / "tilt-sensor-ownership.cpp"
    executable = output / "tilt-sensor-ownership"
    generated.write_text(harness)
    command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Wno-unused-private-field",
        "-I",
        str(test_root / "stubs"),
        "-I",
        str(source_root / "lib/hal"),
        "-I",
        str(source_root / "freeink-sdk/libs/hardware/Imu/include"),
        str(generated),
        str(source_root / "lib/hal/HalTiltSensor.cpp"),
        "-o",
        str(executable),
    ]
    build = subprocess.run(command, text=True, capture_output=True)
    if build.stdout:
        print(build.stdout, end="")
    if build.stderr:
        print(build.stderr, end="", file=sys.stderr)
    if build.returncode:
        sys.exit(build.returncode)

    run = subprocess.run([str(executable)], text=True, capture_output=True)
    if run.stdout:
        print(run.stdout, end="")
    if run.stderr:
        print(run.stderr, end="", file=sys.stderr)
    sys.exit(run.returncode)
