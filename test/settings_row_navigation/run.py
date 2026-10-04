#!/usr/bin/env python3
import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--repo", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--cxx", required=True)
args = parser.parse_args()

root = args.repo.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
(output / "activities/settings").mkdir(parents=True, exist_ok=True)

source = (root / "src/activities/settings/SettingsActivity.h").read_text()
descriptor = source[source.index("enum class SettingType") : source.index("class SettingsActivity final")]
if "settingOpensNext" not in descriptor:
    raise SystemExit("settingOpensNext is missing from the production settings header")

(output / "CrossPointSettings.h").write_text(
    "#pragma once\n"
    "#include <cstdint>\n"
    "struct CrossPointSettings {\n"
    "  uint8_t value = 0;\n"
    "  static CrossPointSettings& getInstance() { static CrossPointSettings settings; return settings; }\n"
    "};\n"
    "#define SETTINGS CrossPointSettings::getInstance()\n"
)
(output / "activities/settings/SettingsActivity.h").write_text(
    "#pragma once\n"
    "#include <functional>\n"
    "#include <span>\n"
    "#include <string>\n"
    "#include <vector>\n"
    "#include <I18n.h>\n"
    '#include "CrossPointSettings.h"\n'
    '#include "activities/settings/SettingsTabs.h"\n'
    + descriptor
)

binary = output / "settings-row-navigation-test"
command = [
    args.cxx,
    "-std=c++20",
    "-Wall",
    "-Wextra",
    "-pedantic",
    "-I" + str(output),
    "-I" + str(root / "src"),
    "-I" + str(root / "lib/I18n"),
    str(Path(__file__).resolve().parent / "SettingsRowNavigationTest.cpp"),
    str(root / "src/activities/settings/SettingsTabs.cpp"),
    "-o",
    str(binary),
]
subprocess.run(command, check=True)
subprocess.run([str(binary)], check=True)

geometry = output / "chevron-geometry-test"
subprocess.run([
    args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror",
    "-I" + str(root / "freeink-sdk/libs/ui/FreeInkUI/include"),
    str(Path(__file__).resolve().parent / "ChevronGeometryTest.cpp"),
    str(root / "freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp"),
    "-o", str(geometry),
], check=True)
subprocess.run([str(geometry), str(output / "chevron.pgm")], check=True)
