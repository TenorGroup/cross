#!/usr/bin/env python3
"""Compile production settings cache lifecycle and launch branch with an allocator probe."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--catalog-release-noop', action='store_true', help='Mutate only the generated release body to prove RED')
a = p.parse_args()
r, out = a.repo.resolve(), a.output.resolve()
out.mkdir(parents=True, exist_ok=True)

def method(text, signature):
    start = text.index(signature)
    i = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[start:i] + '\n'

source = (r / 'src/activities/settings/SettingsActivity.cpp').read_text()
catalog = (r / 'src/SettingsList.h').read_text()
release_signature = 'inline void releaseBaseSettingsList()'
if release_signature not in catalog:
    catalog += '\ninline void releaseBaseSettingsList() {}\n'
elif a.catalog_release_noop:
    catalog = catalog.replace(method(catalog, release_signature), release_signature + ' {}\n')
(out / 'SettingsList.h').write_text(catalog)
(out / 'CrossPointSettings.cpp').write_text((r / 'src/CrossPointSettings.cpp').read_text())
header = (r / 'src/activities/settings/SettingsActivity.h').read_text()
base = (r / 'src/activities/Activity.h').read_text()
(out / 'activities/settings').mkdir(parents=True, exist_ok=True)
descriptor = header[header.index('enum class SettingType'):header.index('class SettingsActivity final')]
(out / 'activities/settings/SettingsActivity.h').write_text(
    '#pragma once\n#include <functional>\n#include <span>\n#include <string>\n#include <vector>\n'
    '#include <I18n.h>\n#include "CrossPointSettings.h"\n'
    '#include "activities/settings/SettingsTabs.h"\n' + descriptor)
methods = ''.join(method(source, sig) for sig in [
    'std::vector<SettingInfo>& SettingsActivity::danhSachCuaThe',
    'void SettingsActivity::rebuildSettingsLists()',
    'void SettingsActivity::rebuildRowItems()'])
for name in ['onPause', 'onResume']:
    signature = 'void SettingsActivity::' + name + '()'
    if signature in source:
        methods += method(source, signature)
    else:
        inherited = method(base, 'virtual void ' + name + '()')
        methods += inherited.replace('virtual void ', 'void SettingsActivity::', 1)
branch = source.split('case SettingAction::DownloadFonts:', 1)[1].split('case SettingAction::TextSettings:', 1)[0]
methods += 'void SettingsActivity::launchFontDownload() {\n' + branch.rsplit('break;', 1)[0] + '}\n'
(out / 'Methods.inc').write_text(methods)
font_source = (r / 'src/activities/settings/FontDownloadActivity.cpp').read_text()
(out / 'PostWifiMethods.inc').write_text(method(font_source, 'void FontDownloadActivity::onWifiSelectionComplete'))
# Reuse the established hardware/storage boundaries, not its SettingsActivity test double.
boundary = (r / 'test/settings_catalog/category/harness.cpp').read_text()
boundary = boundary[boundary.index('HalTiltSensor halTiltSensor;'):boundary.index('struct SettingsActivity {')]
(out / 'Boundaries.inc').write_text(boundary)
for name in ['HalTiltSensor', 'HalClock']:
    instance = name[0].lower() + name[1:]
    (out / (name + '.h')).write_text('#pragma once\nclass ' + name +
        ' { public: bool available=false; bool isAvailable() const { return available; } };\nextern ' +
        name + ' ' + instance + ';\n')
incs = ['test/host_stubs', 'src', '.pio/libdeps/gh_release/ArduinoJson/src',
        'lib/Epub', 'lib/Logging', 'lib/EpdFont', 'lib/Serialization', 'lib/KOReaderSync',
        'freeink-sdk/libs/hardware/BoardConfig/include', 'lib/BlePageTurner/include', 'freeink-sdk/libs/ui/FreeInkUI/include', 'lib/I18n']
cmd = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-g', '-ffunction-sections',
       '-fdata-sections', '-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections',
       '-DFREEINK_DEVICE_X3=1', '-DFREEINK_DEVICE_X4=1', '-DENABLE_ARDUINO_FEATURES=0',
       '-DCROSSPOINT_VERSION="settings-font-memory"', '-I' + str(out)]
if a.sanitize:
    cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
cmd += ['-I' + str(r / x) for x in incs]
cmd += [str(Path(__file__).with_name('harness.cpp')), str(r / 'src/ReaderFontSizes.cpp'),
        str(out / 'CrossPointSettings.cpp'),
        str(r / 'src/activities/settings/SettingsTabs.cpp'), str(r / 'lib/I18n/I18n.cpp'),
        str(r / 'lib/I18n/I18nStrings.cpp'), '-o', str(out / 'probe')]
(out / 'command.json').write_text(json.dumps(cmd, indent=2) + '\n')
(out / 'source-manifest.json').write_text(json.dumps({str(path.relative_to(r)): hashlib.sha256(path.read_bytes()).hexdigest()
    for path in [r / 'src/activities/settings/SettingsActivity.cpp', r / 'src/activities/settings/SettingsActivity.h',
                 r / 'src/SettingsList.h', r / 'src/activities/Activity.h',
                 r / 'src/activities/settings/FontDownloadActivity.cpp', r / 'src/CrossPointSettings.cpp']}, indent=2) + '\n')
build = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
(out / 'build.log').write_text(build.stdout)
if build.returncode:
    print(build.stdout)
    raise SystemExit(build.returncode)
run = subprocess.run([str(out / 'probe')], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
(out / 'run.log').write_text(run.stdout)
print(run.stdout, end='')
raise SystemExit(run.returncode)
