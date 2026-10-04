#!/usr/bin/env python3
"""Compile the production UI settings callback against measured host boundaries."""

import argparse
import hashlib
import json
import os
import pathlib
import subprocess


def method_slice(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    level = 1
    cursor = opening + 1
    while level:
        if text[cursor] == '{':
            level += 1
        elif text[cursor] == '}':
            level -= 1
        cursor += 1
    return text[start:cursor] + '\n'


parser = argparse.ArgumentParser()
parser.add_argument('--repo', required=True, type=pathlib.Path)
parser.add_argument('--output', required=True, type=pathlib.Path)
args = parser.parse_args()
repo = args.repo.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)

source_path = repo / 'src/activities/settings/SettingsActivity.cpp'
source = source_path.read_text()
callback = method_slice(source, 'bool SettingsActivity::applyUiTextSize') + method_slice(source, 'bool SettingsActivity::applyUiSettingChange')
(output / 'ApplyUiSettingChange.inc').write_text(callback)
(output / 'source-manifest.json').write_text(json.dumps({
    'src/activities/settings/SettingsActivity.cpp': hashlib.sha256(source.encode()).hexdigest(),
    'applyUiSettingChange': hashlib.sha256(callback.encode()).hexdigest(),
}, indent=2) + '\n')

binary = output / 'settings-v108-callback'
command = [
    os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
    '-I' + str(output), str(pathlib.Path(__file__).with_name('SettingsV108Callback.cpp')),
    '-o', str(binary),
]
(output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
build = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
(output / 'build.log').write_text(build.stdout)
if build.returncode:
    print(build.stdout, end='')
    raise SystemExit(build.returncode)
run = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
(output / 'run.log').write_text(run.stdout)
(output / 'run.stderr').write_text(run.stderr)
print(run.stdout + run.stderr, end='')
raise SystemExit(run.returncode)
