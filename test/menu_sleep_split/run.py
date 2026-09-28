#!/usr/bin/env python3
"""Compile production category, pin, Home settings, web and persistence paths."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--web-baseline', type=Path)
a = p.parse_args()
r, out = a.repo.resolve(), a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
here = Path(__file__).resolve().parent

def method(text, sig):
    start = text.index(sig)
    i = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[start:i] + '\n'

paths = ['src/SettingsList.h', 'src/MenuCustomization.h', 'src/MenuCustomization.cpp',
         'src/activities/settings/SettingsActivity.h', 'src/activities/settings/SettingsActivity.cpp',
         'src/activities/settings/SettingsTabs.h', 'src/activities/settings/SettingsTabs.cpp',
         'src/activities/home/HomeActivity.cpp', 'src/network/CrossPointWebServer.cpp', 'lib/I18n/I18nKeys.h',
         'src/CrossPointSettings.cpp']
(out / 'source-manifest.json').write_text(json.dumps({x: hashlib.sha256((r/x).read_bytes()).hexdigest() for x in paths}, indent=2)+'\n')
(out / 'SettingsList.h').write_text((r / paths[0]).read_text())
header = (r / paths[3]).read_text()
source = (r / paths[4]).read_text()
(out / 'activities/settings').mkdir(parents=True, exist_ok=True)
(out / 'activities/settings/SettingsActivity.h').write_text(
    '#pragma once\n#include <functional>\n#include <span>\n#include <string>\n#include <vector>\n#include <I18n.h>\n'
    '#include "CrossPointSettings.h"\n#include "activities/settings/SettingsTabs.h"\n' +
    header[header.index('enum class SettingType'):header.index('class SettingsActivity final')])
category = (r / 'test/settings_catalog/category/harness.cpp').read_text()
boundaries = category[category.index('HalTiltSensor halTiltSensor;'):category.index('struct SettingsActivity {')]
(out / 'Boundaries.inc').write_text(boundaries)
fields = header[header.index('  std::vector<SettingInfo> displaySettings;'):header.index('  const std::vector<SettingInfo>* currentSettings')]
(out / 'Fields.inc').write_text(fields)
sigs = ['std::vector<SettingInfo>& SettingsActivity::danhSachCuaThe', 'void SettingsActivity::rebuildSettingsLists()',
        'std::string SettingsActivity::favoriteKey(', 'int SettingsActivity::focusFavorite(']
methods = ''.join(method(source, s) for s in sigs)
(out / 'Methods.inc').write_text(methods)
home = (r / 'src/activities/home/HomeActivity.cpp').read_text()
home = home[home.index('case Tab::CAI_DAT: {'):]
home = home[:home.index('\n    }')]
(out / 'HomeSettings.inc').write_text(home.split('{', 1)[1].replace('      break;', ''))
web = (r / 'src/network/CrossPointWebServer.cpp').read_text()
(out / 'Web.inc').write_text(method(web, 'void CrossPointWebServer::handleGetSettings() const'))
# The serializer first adopts the status bar switches for the stored mode, so the
# production adopt step and the mode table it reads are compiled in as well.
settings = (r / 'src/CrossPointSettings.cpp').read_text()
adopt = 'void CrossPointSettings::adoptReaderStatusItems()'
(out / 'StatusItems.inc').write_text(
    settings[settings.index('namespace {\n// What each reader status bar mode shows'):settings.index(adopt)] +
    method(settings, adopt))
keys = (r / 'lib/I18n/I18nKeys.h').read_text()
keys = re.findall(r'^\s*(STR_[A-Z0-9_]+)\s*,', keys[keys.index('enum class StrId'):], re.M)
(out / 'KeyNames.inc').write_text(',\n'.join(json.dumps(x) for x in keys))
for name in ['HalTiltSensor', 'HalClock']:
    instance = name[0].lower() + name[1:]
    (out / (name+'.h')).write_text('#pragma once\nclass '+name+' { public: bool available=false; bool isAvailable() const { return available; } };\nextern '+name+' '+instance+';\n')
incs = ['test/host_stubs', 'src', '.pio/libdeps/gh_release/ArduinoJson/src', 'lib/Epub', 'lib/Logging',
        'lib/EpdFont', 'lib/Serialization', 'lib/KOReaderSync', 'lib/I18n',
        'freeink-sdk/libs/hardware/BoardConfig/include', 'lib/BlePageTurner/include', 'freeink-sdk/libs/ui/FreeInkUI/include']
common = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-g', '-ffunction-sections', '-fdata-sections',
          '-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections',
          '-DENABLE_ARDUINO_FEATURES=0', '-DCROSSPOINT_VERSION="sleep-split"', '-I'+str(out)]
if a.sanitize:
    common += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
common += ['-I'+str(r/x) for x in incs]
expected_web = json.loads((here/'web-schema-sha256.json').read_text())
results = []
for profile in ['c3', 'pro', 'persistence']:
    defines = ['-DFREEINK_DEVICE_X4PRO=1'] if profile == 'pro' else ['-DFREEINK_DEVICE_X3=1', '-DFREEINK_DEVICE_X4=1']
    if profile == 'persistence':
        cmd = [common[0], '-I'+str(here/'stubs'), *common[1:], str(here/'persistence.cpp')]
    else:
        cmd = common + defines + [str(here/'harness.cpp'), str(r/'src/ReaderFontSizes.cpp'),
               str(r/'src/activities/settings/SettingsTabs.cpp'), str(r/'lib/I18n/I18n.cpp'), str(r/'lib/I18n/I18nStrings.cpp')]
    cmd += ['-o', str(out/profile)]
    (out/(profile+'-command.json')).write_text(json.dumps(cmd, indent=2)+'\n')
    build = subprocess.run(cmd, capture_output=True, text=True)
    (out/(profile+'-build.log')).write_text(build.stdout+build.stderr)
    if build.returncode:
        print(build.stderr)
        raise SystemExit(build.returncode)
    if profile == 'persistence':
        cases = [([], 'persistence')]
    else:
        cases = [([board, str(theme), str(imu), str(optional)], f'{board}-{theme}-{imu}-{optional}')
                 for board in (['pro'] if profile == 'pro' else ['x3', 'x4'])
                 for theme in [0, 1] for imu in [0, 1] for optional in [0, 1]]
    for argv, case in cases:
        run = subprocess.run([str(out/profile), *argv], capture_output=True, text=True)
        (out/(case+'.log')).write_text(run.stdout+run.stderr)
        result = {'case':case, 'exit':run.returncode}
        if profile != 'persistence':
            measured = json.loads(run.stdout)
            (out/(case+'-web.json')).write_text(json.dumps(measured['web'], sort_keys=True, indent=2)+'\n')
            result.update({k:v for k,v in measured.items() if k != 'web'})
            if hashlib.sha256((out/(case+'-web.json')).read_bytes()).hexdigest() != expected_web[case]:
                result['web_schema_mismatch'] = True
                result['exit'] = 1
            if a.web_baseline:
                old = json.loads((a.web_baseline/(case+'-web.json')).read_text())
                if old != measured['web']:
                    result['web_schema_mismatch'] = True
                    result['exit'] = 1
        results.append(result)
        print(case, 'PASS' if result['exit'] == 0 else 'FAIL', run.stderr.strip())
(out/'results.json').write_text(json.dumps(results, indent=2)+'\n')
raise SystemExit(any(x['exit'] for x in results))
