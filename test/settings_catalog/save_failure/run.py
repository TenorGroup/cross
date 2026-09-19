#!/usr/bin/env python3
"""Exercise production settings callbacks and Back across injected save failures.

Current-only mode needs no git. --baseline-dir reads frozen pre-fix source for
RED evidence; --baseline-ref optionally reads an available git revision.
"""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--baseline-dir', type=pathlib.Path)
parser.add_argument('--baseline-ref')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
ROOT = args.repo.resolve()
OUT = args.output.resolve()
OUT.mkdir(parents=True, exist_ok=True)


def method_slice(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    level = 1
    i = opening + 1
    while level:
        if text[i] == '{':
            level += 1
        if text[i] == '}':
            level -= 1
        i += 1
    return text[start:i] + '\n'


include_paths = [
    'test/host_stubs', 'src', '.pio/libdeps/gh_release/ArduinoJson/src',
    'lib/Epub', 'lib/I18n', 'lib/Logging', 'lib/EpdFont', 'lib/Serialization',
    'lib/KOReaderSync', 'freeink-sdk/libs/hardware/BoardConfig/include',
    'freeink-sdk/libs/ui/FreeInkUI/include',
]
includes = ['-I' + str(ROOT / path) for path in include_paths]
manifest = {}
results = {}
modes = ['before', 'after'] if args.baseline_ref or args.baseline_dir else ['after']
for mode in modes:
    output = OUT / mode
    output.mkdir(exist_ok=True)
    (output / 'activities/settings').mkdir(parents=True, exist_ok=True)

    def read(path):
        if mode == 'before':
            if args.baseline_dir:
                return (args.baseline_dir / ('before-' + pathlib.Path(path).name)).read_text()
            return subprocess.check_output(
                ['git', 'show', f'{args.baseline_ref}:{path}'], cwd=ROOT, text=True)
        return (ROOT / path).read_text()

    paths = ['src/activities/settings/SettingsActivity.h',
             'src/activities/settings/SettingsActivity.cpp']
    sources = {path: read(path) for path in paths}
    manifest[mode] = {path: hashlib.sha256(text.encode()).hexdigest()
                      for path, text in sources.items()}
    manifest[mode]['lib/Serialization/PersistableStore.h'] = hashlib.sha256(
        (ROOT / 'lib/Serialization/PersistableStore.h').read_bytes()).hexdigest()
    header = sources[paths[0]]
    descriptor = header[header.index('enum class SettingType'):
                        header.index('class SettingsActivity final')]
    (output / 'activities/settings/SettingsActivity.h').write_text(
        '#pragma once\n#include <functional>\n#include <string>\n#include <vector>\n'
        '#include <I18n.h>\n#include "CrossPointSettings.h"\n'
        '#include "activities/settings/SettingsTabs.h"\n' + descriptor)
    state = next((line.strip() for line in header.splitlines()
                  if 'saveFailed' in line and ';' in line), '')
    (output / 'State.inc').write_text(state + '\n')
    source = sources[paths[1]]
    signatures = ['bool SettingsActivity::handleButtons()',
                  'void SettingsActivity::toggleCurrentSetting()',
                  'void SettingsActivity::openSleepTimeoutPicker()',
                  'void SettingsActivity::render(RenderLock&&)']
    if 'bool SettingsActivity::saveSettings()' in source:
        signatures.insert(0, 'bool SettingsActivity::saveSettings()')
    (output / 'Methods.inc').write_text(''.join(method_slice(source, signature)
                                               for signature in signatures))
    sanitizer = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
    dead_strip = '-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-g', '-O1', *sanitizer,
               '-ffunction-sections', '-fdata-sections', dead_strip,
               '-DENABLE_ARDUINO_FEATURES=0', '-DCROSSPOINT_VERSION="save-failure-test"',
               '-DFREEINK_DEVICE_X3=1', '-DFREEINK_DEVICE_X4=1',
               '-I' + str(output), *includes, str(HERE / 'harness.cpp'),
               str(ROOT / 'src/activities/settings/SettingsTabs.cpp'),
               str(ROOT / 'lib/I18n/I18n.cpp'), str(ROOT / 'lib/I18n/I18nStrings.cpp'),
               '-o', str(output / 'save-failure')]
    (output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    build = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (output / 'build.log').write_text(build.stdout)
    if build.returncode:
        print(build.stdout)
        raise SystemExit(build.returncode)
    run = subprocess.run([str(output / 'save-failure')], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    (output / 'run.log').write_text(run.stdout)
    (output / 'run.stderr').write_text(run.stderr)
    expected_exit = 1 if mode == 'before' else 0
    results[mode] = {'exit': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr}
    if run.returncode != expected_exit:
        print(mode, run.returncode, run.stdout, run.stderr)
        raise SystemExit(1)
    print(mode + ': ' + run.stdout.strip())
(OUT / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
