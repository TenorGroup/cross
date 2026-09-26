#!/usr/bin/env python3
"""Execute unmodified production sleep bodies against controllable boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def extract(source, expression):
    match = re.search(expression, source)
    if not match:
        raise ValueError(f"Missing function: {expression}")
    start = match.start()
    brace = source.index("{", match.start())
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise ValueError("Unclosed function")


parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--compiler", default="c++")
args = parser.parse_args()
args.source = args.source.resolve()
args.output = args.output.resolve()
args.output.mkdir(parents=True, exist_ok=True)
activity_path = args.source / "src/activities/ActivityManager.cpp"
main_path = args.source / "src/main.cpp"
go = extract(activity_path.read_text(), r"(?:bool|void) ActivityManager::goToSleep\(")
main_source = main_path.read_text()
tilt_update = re.search(
    r"const bool pendingTiltActivity = halTiltSensor\.hadActivity\(\);\s+"
    r"(?:halTiltSensor\.noteScreen\(activityManager\.activityGeneration\(\)\);\s+)?"
    r"const bool foregroundReader = activityManager\.isForegroundReaderActivity\(\);\s+"
    r"const bool foregroundActivityManagesTiltSensor = activityManager\.isForegroundActivityManagingTiltSensor\(\);\s+"
    r"updateTiltSensorForForegroundActivity\(foregroundReader, foregroundActivityManagesTiltSensor\);",
    main_source,
)
if not tilt_update:
    raise ValueError("Missing foreground tilt poll")
tilt_input = re.search(r"const bool nguoiDungChamVao\s*=\s*.*?;", main_source, re.DOTALL)
if not tilt_input:
    raise ValueError("Missing accepted user activity input")
tilt_activity_gate = tilt_update.group() + "\n" + tilt_input.group()
tilt_route = extract(main_source, r"static void updateTiltSensorForForegroundActivity\(")
enter = extract(main_source, r"void enterDeepSleep\(")
input_clock = extract(main_source, r"if \(userActivity\)")
reset_clock = re.search(r"if \(userActivity \|\| activityManager\.preventAutoSleep\(\)\) lastSleepResetTime = millis\(\);", main_source).group()
auto_sleep = extract(main_source, r"if \(sleepTimeoutMs > 0")
sleep_gate = input_clock + "\n" + reset_clock + "\nconst unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();\n" + auto_sleep
return_type = go.split(" ", 1)[0]
template = (Path(__file__).parent / "harness.cpp.in").read_text()
generated = (template.replace("@RETURN_TYPE@", return_type)
             .replace("@GO_TO_SLEEP@", go)
             .replace("@ENTER_DEEP_SLEEP@", enter)
             .replace("@MAIN_SLEEP_GATE@", sleep_gate)
             .replace("@TILT_ROUTE@", tilt_route)
             .replace("@MAIN_TILT_ACTIVITY_GATE@", tilt_activity_gate))
cpp = args.output / "sleep-regression.cpp"
cpp.write_text(generated)
manifest = {
    "source": str(args.source.resolve()),
    "production_functions": {
        "ActivityManager::goToSleep": {"file": str(activity_path), "sha256": hashlib.sha256(go.encode()).hexdigest()},
        "enterDeepSleep": {"file": str(main_path), "sha256": hashlib.sha256(enter.encode()).hexdigest()},
        "main sleep gate and accepted-input clocks": {"file": str(main_path), "sha256": hashlib.sha256(sleep_gate.encode()).hexdigest()},
        "main foreground tilt poll and input gate": {"file": str(main_path), "sha256": hashlib.sha256(tilt_activity_gate.encode()).hexdigest()},
    },
    "boundary_model": "Monotonic fake clock; deferred activity loop; BLE suspension availability; fake storage/display/deep sleep. Production sleep bodies plus the main tilt poll and accepted-input gate execute unchanged.",
}
exe = args.output / "sleep-regression"
build = subprocess.run([args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-variable", str(cpp), "-o", str(exe)], cwd=args.output, text=True, capture_output=True)
(args.output / "build.log").write_text(build.stdout + build.stderr)
if build.returncode:
    print(build.stdout + build.stderr)
    sys.exit(build.returncode)
run = subprocess.run([str(exe)], cwd=args.output, text=True, capture_output=True)
(args.output / "test.log").write_text(run.stdout + run.stderr)
manifest["exit_code"] = run.returncode
manifest["result"] = "GREEN" if run.returncode == 0 else "RED"
(args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(run.stdout + run.stderr, end="")
sys.exit(run.returncode)
