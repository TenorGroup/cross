#!/usr/bin/env python3
"""Run production BLE menu methods with fake radio/UI boundaries, without firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--compiler", default="c++")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)


def block(text, brace):
    depth, i, state = 1, brace + 1, "code"
    while depth:
        c, n = text[i], text[i + 1:i + 2]
        if state == "line":
            if c == "\n":
                state = "code"
        elif state == "comment":
            if c == "*" and n == "/":
                state, i = "code", i + 1
        elif state in ('"', "'"):
            if c == "\\":
                i += 1
            elif c == state:
                state = "code"
        elif c == "/" and n == "/":
            state, i = "line", i + 1
        elif c == "/" and n == "*":
            state, i = "comment", i + 1
        elif c in ('"', "'"):
            state = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return text[brace:i]


def function(text, name):
    match = re.search(r"^(?:void|bool) " + re.escape(name) + r"\(", text, re.M)
    assert match, name
    brace = text.index("{", match.start())
    return text[match.start():brace] + block(text, brace)


paths = ["src/activities/settings/BlePageTurnerActivity.cpp",
         "src/activities/settings/BlePageTurnerActivity.h",
         "src/activities/reader/ReaderMenuLayout.cpp", "src/activities/reader/ReaderMenuLayout.h",
         "src/activities/reader/EpubReaderActivity.cpp", "src/activities/UiListActivity.cpp"]
texts = {p: (args.source / p).read_text() for p in paths}
body, header = texts[paths[0]], texts[paths[1]]
row_start = header.index("enum RowCode")
row_brace = header.index("{", row_start)
row_enum = header[row_start:row_brace] + block(header, row_brace) + ";"
names = ["rebuildRows", "refreshValues", "activateIndex", "openPairedPopup", "toggleEnabled", "handleScanRow",
         "clearBindForRow"]
methods = [function(body, "BlePageTurnerActivity::" + name) for name in names]
for name in ["stepSelection", "clampAfterNav"]:
    if "BlePageTurnerActivity::" + name in body:
        methods.append(function(body, "BlePageTurnerActivity::" + name))
    elif name == "stepSelection":
        methods.append("void BlePageTurnerActivity::stepSelection(int direction) { UiListActivity::stepSelection(direction); }")
    else:
        methods.append("bool BlePageTurnerActivity::clampAfterNav() { return false; }")
methods.append(function(texts[paths[5]], "UiListActivity::stepSelection"))
route = texts[paths[4]]
marker = "case EpubReaderMenuActivity::MenuAction::BLUETOOTH:"
route_body = "{}"
if marker in route:
    route_body = block(route, route.index("{", route.index(marker)))
fixture = Path(__file__).with_name("fixture.cpp.in").read_text()
fixture = fixture.replace("@@ROW_ENUM@@", row_enum).replace("@@METHODS@@", "\n".join(methods))
fixture = fixture.replace("@@ROUTE@@", "switch (20) { case 20: " + route_body + " }")
cpp = args.output / "production-menu.cpp"
cpp.write_text(fixture)
exe = args.output / "ble-menu-ux"
command = [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
           "-I" + str(args.source / "src"), "-I" + str(args.source / "lib/I18n"), str(cpp),
           str(args.source / paths[2]), "-o", str(exe)]
compile_result = subprocess.run(command, text=True, capture_output=True)
(args.output / "compile.log").write_text(compile_result.stdout + compile_result.stderr)
result = {"command": command, "compile_returncode": compile_result.returncode,
          "sha256": {p: hashlib.sha256((args.source / p).read_bytes()).hexdigest() for p in paths},
          "boundary": "Production row, activation, popup, navigation and reader route methods; fake UI, settings and radio. No RF/display emulation."}
if compile_result.returncode == 0:
    run = subprocess.run([str(exe)], text=True, capture_output=True)
    (args.output / "run.log").write_text(run.stdout + run.stderr)
    result["run_returncode"] = run.returncode
    print(run.stdout + run.stderr, end="")
else:
    print(compile_result.stdout + compile_result.stderr)
(args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
raise SystemExit(result.get("run_returncode", compile_result.returncode))
