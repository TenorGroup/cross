#!/usr/bin/env python3
"""Exercise the production web callback and verify both activity owners inject it."""

import argparse
import hashlib
import json
import os
import pathlib
import subprocess


def method_slice(text, signature):
    if signature not in text:
        print(f"RED: missing production method {signature}")
        raise SystemExit(1)
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

server_path = repo / 'src/network/CrossPointWebServer.cpp'
server_source = server_path.read_text()
setter = method_slice(server_source, 'void CrossPointWebServer::setUiTextSizeApplier')
apply = method_slice(server_source, 'bool CrossPointWebServer::applyUiTextSizeSetting')
post = method_slice(server_source, 'void CrossPointWebServer::handlePostSettings')
if post.index('applyUiTextSizeSetting') > post.index('for (const auto& s : settings)'):
    print('RED: UI text size apply is not preflighted before generic assignment')
    raise SystemExit(1)
if 's.valuePtr != &CrossPointSettings::uiTextSize' not in post:
    print('RED: generic assignment republishes UI text size outside the activity lock')
    raise SystemExit(1)
if 'SETTINGS.saveToFile()' not in post or 'applyUiTextSizeSetting(previousUiTextSize)' not in post:
    print('RED: save failure does not roll UI text size back')
    raise SystemExit(1)

activity_paths = [
    repo / 'src/activities/network/CrossPointWebServerActivity.cpp',
    repo / 'src/activities/network/CalibreConnectActivity.cpp',
]
for path in activity_paths:
    start = method_slice(path.read_text(), f'void {path.stem}::startWebServer')
    required = ['setUiTextSizeApplier', 'RenderLock', 'applyUiFontSize',
                'SETTINGS.uiTextSize', 'UITheme::getInstance().reload()', 'requestUpdate()']
    missing = [token for token in required if token not in start]
    if missing:
        print(f"RED: {path.name} missing web UI size injection: {', '.join(missing)}")
        raise SystemExit(1)

(output / 'WebUiTextSizeMethods.inc').write_text(setter + apply)
(output / 'source-manifest.json').write_text(json.dumps({
    str(server_path.relative_to(repo)): hashlib.sha256(server_source.encode()).hexdigest(),
    **{str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest() for path in activity_paths},
}, indent=2) + '\n')

binary = output / 'web-ui-text-size'
command = [
    os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(output),
    str(pathlib.Path(__file__).with_name('WebUiTextSizeCallback.cpp')), '-o', str(binary),
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
