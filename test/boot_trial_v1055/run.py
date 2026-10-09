#!/usr/bin/env python3
"""Exercise production BootTrial and exact startup routing with virtual ESP timers."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--cxx', default='c++')
parser.add_argument('--main-source', type=Path)
parser.add_argument('--boot-source', type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
source = (args.main_source or repo / 'src/main.cpp').read_text()
if 'const bool trialBoot = boot_trial::begin();' in source:
    begin = source[source.index('#if FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)\n  const bool trialBoot'):]
else:
    begin = source[source.index('#if FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)\n  boot_trial::begin();'):]
begin = begin[:begin.index('#endif') + len('#endif')]
route_start = source.index('  if (recoveryFirmwareMode) {\n    // Skip normal home/reader routing:')
route_end = source.index('\n  if (resume == BootResume::Silent) {\n    // Block until', route_start)
accept_start = source.index('#ifndef SIMULATOR\n  esp_ota_img_states_t otaState;')
accept_end = source.index('\n  allowSleepAt =', accept_start)
(out / 'adapter.inc').write_text(begin + '\n' + source[route_start:route_end] + '\n' + source[accept_start:accept_end])
binary = out / 'boot-trial'
boot_source = args.boot_source or repo / 'src/platform/BootTrial.cpp'
subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-DFREEINK_DEVICE_X4PRO=1',
                '-I' + str(here / 'stubs'), '-I' + str(boot_source.parent), '-I' + str(repo / 'src/platform'),
                '-I' + str(out), str(here / 'BootTrial.cpp'), str(boot_source),
                '-o', str(binary)], check=True)
cases = ['valid', 'new', 'aborted', 'query-error', 'pass', 'recovery', 'no-touch', 'mark-error',
         'create-error', 'start-error', 'hung-paint', 'valid-race', 'silent-large-book',
         'wake-large-book', 'valid-wake-book']
results = []
for case in cases:
    proc = subprocess.run([str(binary), case], text=True, capture_output=True)
    print(proc.stdout, end='')
    if proc.stderr:
        print(proc.stderr, end='')
    results.append({'case': case, 'exit': proc.returncode, 'stdout': proc.stdout, 'stderr': proc.stderr})
(out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
passed = sum(result['exit'] == 0 for result in results)
print(f'{passed}/{len(results)} passed')
raise SystemExit(passed != len(results))
