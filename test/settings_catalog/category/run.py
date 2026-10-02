#!/usr/bin/env python3
"""Compile unchanged settings category method bodies against hardware boundaries.

--baseline-ref optionally records RED on an earlier revision. The default
current-only mode uses the recorded category order fixture and needs no git.
"""
import argparse
import hashlib
import itertools
import json
import os
import pathlib
import platform
import re
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=pathlib.Path, required=True)
parser.add_argument('--output', type=pathlib.Path, required=True)
parser.add_argument('--baseline-ref')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--update-fixture', action='store_true')
parser.add_argument('--i18n-dir', type=pathlib.Path)
args = parser.parse_args()
ROOT = args.repo.resolve()
OUT = args.output.resolve()
I18N = args.i18n_dir.resolve() if args.i18n_dir else ROOT / 'lib/I18n'
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
    'lib/Epub', 'lib/Logging', 'lib/EpdFont', 'lib/Serialization',
    'lib/KOReaderSync', 'freeink-sdk/libs/hardware/BoardConfig/include', 'lib/BlePageTurner/include',
    'freeink-sdk/libs/ui/FreeInkUI/include',
]
includes = ['-I' + str(I18N), *['-I' + str(ROOT / path) for path in include_paths]]


def load_str_ids(path):
    text = path.read_text()
    start = text.index('enum class StrId')
    sentinel = re.search(r'^\s*_COUNT\s*$', text[start:], re.MULTILINE)
    if sentinel is None:
        raise RuntimeError('StrId sentinel missing from generated keyset')
    body = text[start:start + sentinel.start()]
    return re.findall(r'^\s*(STR_[A-Z0-9_]+)\s*,', body, re.MULTILINE)


str_ids = load_str_ids(I18N / 'I18nKeys.h')


def symbolic_rows(measured):
    rows = []
    for tab in measured['tabs']:
        symbolic = []
        for row in tab['rows']:
            label, remainder = row.split(':', 1)
            index = int(label)
            if index >= len(str_ids):
                raise RuntimeError(f'StrId ordinal {index} exceeds generated keyset')
            symbolic.append(f'{str_ids[index]}:{remainder}')
        rows.append(symbolic)
    return rows


manifest = {}
modes = ['before', 'after'] if args.baseline_ref else ['after']
for mode in modes:
    output = OUT / mode
    output.mkdir(exist_ok=True)
    (output / 'activities/settings').mkdir(parents=True, exist_ok=True)

    def read(path):
        if mode == 'before':
            return subprocess.check_output(
                ['git', 'show', f'{args.baseline_ref}:{path}'], cwd=ROOT, text=True)
        return (ROOT / path).read_text()

    paths = ['src/SettingsList.h', 'src/activities/settings/SettingsActivity.h',
             'src/activities/settings/SettingsActivity.cpp']
    sources = {path: read(path) for path in paths}
    manifest[mode] = {path: hashlib.sha256(text.encode()).hexdigest()
                      for path, text in sources.items()}
    manifest[mode]['I18nKeys.h'] = hashlib.sha256((I18N / 'I18nKeys.h').read_bytes()).hexdigest()
    (output / 'SettingsList.h').write_text(sources[paths[0]])
    header = sources[paths[1]]
    descriptor = header[header.index('enum class SettingType'):
                        header.index('class SettingsActivity final')]
    (output / 'activities/settings/SettingsActivity.h').write_text(
        '#pragma once\n#include <functional>\n#include <span>\n#include <string>\n#include <vector>\n'
        '#include <I18n.h>\n#include "CrossPointSettings.h"\n'
        '#include "activities/settings/SettingsTabs.h"\n' + descriptor)
    methods = method_slice(sources[paths[2]],
                           'std::vector<SettingInfo>& SettingsActivity::danhSachCuaThe')
    methods += method_slice(sources[paths[2]], 'void SettingsActivity::rebuildSettingsLists()')
    (output / 'CategoryMethods.inc').write_text(methods)
    for name in ['HalTiltSensor', 'HalClock']:
        instance = name[0].lower() + name[1:]
        (output / f'{name}.h').write_text(
            '#pragma once\nclass ' + name + ' { public: bool available=false; '
            'bool isAvailable() const { return available; } };\n'
            'extern ' + name + ' ' + instance + ';\n')
    for profile in ['c3', 'pro']:
        devices = (['-DFREEINK_DEVICE_X3=1', '-DFREEINK_DEVICE_X4=1'] if profile == 'c3'
                   else ['-DFREEINK_DEVICE_X4PRO=1'])
        sanitizer = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        dead_strip = '-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections'
        command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-g', '-O1', *sanitizer,
                   '-ffunction-sections', '-fdata-sections', dead_strip,
                   '-DENABLE_ARDUINO_FEATURES=0', '-DCROSSPOINT_VERSION="category-test"',
                   *devices, '-I' + str(output), *includes, str(HERE / 'harness.cpp'),
                   str(ROOT / 'src/ReaderFontSizes.cpp'),
                   str(ROOT / 'src/activities/settings/SettingsTabs.cpp'),
                   str(I18N / 'I18n.cpp'), str(I18N / 'I18nStrings.cpp'),
                   '-o', str(output / profile)]
        (output / (profile + '-command.json')).write_text(json.dumps(command, indent=2) + '\n')
        build = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (output / (profile + '-build.log')).write_text(build.stdout)
        if build.returncode:
            print(build.stdout)
            raise SystemExit(build.returncode)

fixture = json.loads((HERE / 'expected-rows.json').read_text())
updated_fixture = {}
results = []
for board, rtc, footnotes, dictionaries in itertools.product(
        ['x3', 'x4', 'pro'], [0, 1], [0, 1], [0, 1]):
    pair = {}
    case = '-'.join(map(str, [board, rtc, footnotes, dictionaries]))
    for mode in modes:
        binary = OUT / mode / ('pro' if board == 'pro' else 'c3')
        command = [str(binary), '--enforce', board, str(rtc), str(footnotes), str(dictionaries)]
        run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        (OUT / mode / (case + '.stderr')).write_text(run.stderr)
        (OUT / mode / (case + '.json')).write_text(run.stdout)
        try:
            measured = json.loads(run.stdout)
        except json.JSONDecodeError:
            print(case, mode, run.returncode, run.stderr)
            raise SystemExit(1)
        measured['exit'] = run.returncode
        pair[mode] = measured
        expected_exit = 1 if mode == 'before' else 0
        if run.returncode != expected_exit:
            print(case, mode, run.returncode, run.stderr)
            raise SystemExit(1)
        rows = symbolic_rows(measured)
        if mode == 'after':
            updated_fixture[case] = rows
        if not args.update_fixture and rows != fixture[case]:
            print(case, mode, 'category row order changed')
            raise SystemExit(1)
    results.append({'case': case, **pair})
if args.update_fixture:
    (HERE / 'expected-rows.json').write_text(json.dumps(updated_fixture, indent=2) + '\n')
(OUT / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
(OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
red_note = 'baseline allocation/capacity assertions RED; ' if args.baseline_ref else ''
sanitizer_note = ' under ASan/UBSan' if args.sanitize else ''
fixture_note = ('category fixture updated from measured current output' if args.update_fixture
                else 'exact row order identical to baseline fixture')
print(f'{len(results)} cases: {red_note}current GREEN. {fixture_note}; '
      f'dynamic owned-lifetime checks GREEN{sanitizer_note}.')
