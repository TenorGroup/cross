#!/usr/bin/env python3
"""Run the complete production transfer loop against input and network boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--source', type=Path, help='Frozen production cpp for a before-fix run')
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[1]
    source = args.source or repo / 'src/activities/network/CrossPointWebServerActivity.cpp'
    code = source.read_text()
    start = code.index('void CrossPointWebServerActivity::loop() {')
    end = code.index('\nvoid CrossPointWebServerActivity::render(', start)
    loop = code[start:end]
    header = (repo / 'src/activities/network/CrossPointWebServerActivity.h').read_text()
    delay = next(line.strip().replace(' override', '') for line in header.splitlines()
                 if 'bool skipLoopDelay()' in line)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'production-loop.inc').write_text(loop)
    (output / 'production-delay.inc').write_text(delay + '\n')
    hashes = {str(source): hashlib.sha256(source.read_bytes()).hexdigest()}
    for name in ('src/main.cpp', 'src/MappedInputManager.cpp',
                 'freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp'):
        hashes[name] = hashlib.sha256((repo / name).read_bytes()).hexdigest()
    (output / 'source-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
    binary = output / 'file-transfer-loop'
    subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(output), str(here / 'FileTransferLoop.cpp'), '-o', str(binary)], check=True)
    cases = ('entry-back', 'entry-home', 'loaded-handler-budget', 'release-after-handler',
             'upload-boundary-exit', 'ap-dns-and-throughput', 'wifi-recovery',
             'wifi-abandon', 'stopped-server-back', 'idle-timeout-safe-boundary',
             'recent-transfer-activity-defers-timeout', 'inactive-state')
    results = []
    for case in cases:
        run = subprocess.run([str(binary), case], capture_output=True, text=True)
        results.append({'case': case, 'exit': run.returncode, 'log': run.stdout + run.stderr})
        print(run.stdout + run.stderr, end='')
    (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    (output / 'result.log').write_text(''.join(row['log'] for row in results))
    passed = sum(row['exit'] == 0 for row in results)
    print(f'{passed}/{len(results)} scenarios passed')
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
