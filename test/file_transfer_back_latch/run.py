#!/usr/bin/env python3
"""Compile production sampler and full transfer loop with timed physical input."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--before-loop', type=Path)
p.add_argument('--cxx', default='c++')
a = p.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
source = repo / 'src/activities/network/CrossPointWebServerActivity.cpp'
code = source.read_text()
start = code.index('void CrossPointWebServerActivity::loop() {')
end = code.index('\nvoid CrossPointWebServerActivity::render(', start)
loop = a.before_loop.read_text() if a.before_loop else code[start:end]
(out / 'production-loop.inc').write_text(loop)
begin_start = code.index('void CrossPointWebServerActivity::startWebServer() {')
(out / 'production-start.inc').write_text(code[begin_start:start])
header = (repo / 'src/activities/network/CrossPointWebServerActivity.h').read_text()
(out / 'production-delay.inc').write_text(next(line.strip().replace(' override', '') for line in header.splitlines() if 'bool skipLoopDelay()' in line))
sdk = (repo / 'freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp').read_text()
def extract(signature):
    begin = sdk.index(signature)
    at = sdk.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (sdk[at] == '{') - (sdk[at] == '}')
        at += 1
    return sdk[begin:at].replace('InputManager::', 'PhysicalInput::') + '\n'
(out / 'production-input.inc').write_text(extract('void InputManager::applyStateChange(') + extract('void InputManager::update()'))
helper = repo / 'src/util/FileTransferBackLatch.cpp'
(out / 'source-hashes.json').write_text(json.dumps({str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in [source, helper, here / 'BackLatch.cpp']} | {'compiled-loop': hashlib.sha256(loop.encode()).hexdigest(), 'compiled-input': hashlib.sha256((out / 'production-input.inc').read_bytes()).hexdigest()}, indent=2) + '\n')
binary = out / 'back-latch'
subprocess.run([a.cxx, '-std=c++17', '-pthread', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-DCROSSPOINT_EMULATED=0', '-I' + str(here / 'stubs'), '-I' + str(repo / 'src/util'), '-I' + str(out), str(helper), str(here / 'BackLatch.cpp'), '-o', str(binary)], check=True)
cases = ('physical-pulse-in-handler', 'idle-pulse', 'held-on-entry', 'reentry-clears-old', 'unmapped', 'zero-chatter-mapping3', 'oom-start', 'oom-activity', 'ui-size-callback', 'zero-chatter-activity', 'unsupported-board', 'destruction-joins', 'mapping-out-of-range', 'stalled-upload-back', 'back-on-side-key-in-handler', 'second-ladder-back')
results = []
for name in cases:
    run = subprocess.run([str(binary), name], capture_output=True, text=True)
    results.append({'case': name, 'exit': run.returncode, 'log': run.stdout + run.stderr})
    print(run.stdout + run.stderr, end='')
(out / 'result.log').write_text(''.join(row['log'] for row in results))
(out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
print(f"{sum(r['exit'] == 0 for r in results)}/{len(cases)} passed")
raise SystemExit(int(any(r['exit'] for r in results)))
