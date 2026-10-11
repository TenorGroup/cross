#!/usr/bin/env python3
"""Build the production File transfer entry and check what it holds when the radio starts."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cxx', default='c++')
a = p.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
code = (repo / 'src/activities/network/CrossPointWebServerActivity.cpp').read_text()
start = code.index('void CrossPointWebServerActivity::onEnter() {')
at = code.index('{', start) + 1
depth = 1
while depth:
    depth += (code[at] == '{') - (code[at] == '}')
    at += 1
(out / 'production-onenter.inc').write_text(code[start:at] + '\n')
failed = 0
for name, flags in (('menu', []), ('auto-join', ['-DTENOR_UI_ACCEPTANCE'])):
    binary = out / f'onenter-{name}'
    subprocess.run([a.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', *flags,
                    '-I' + str(out), str(here / 'Harness.cpp'), '-o', str(binary)], check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    print(f'{name}: {run.stdout.strip()}')
    failed |= run.returncode
code = (repo / 'src/activities/settings/OtaUpdateActivity.cpp').read_text()
start = code.index('void OtaUpdateActivity::onEnter() {')
at = code.index('{', start) + 1
depth = 1
while depth:
    depth += (code[at] == '{') - (code[at] == '}')
    at += 1
(out / 'production-ota-enter.inc').write_text(code[start:at] + '\n')
binary = out / 'ota-entry'
subprocess.run([a.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I' + str(out), str(here / 'OtaEntry.cpp'), '-o', str(binary)], check=True)
failed |= subprocess.run([str(binary)]).returncode
raise SystemExit(failed)
