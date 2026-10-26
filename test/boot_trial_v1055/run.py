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
# v1.1.0: the X4 Classic is built as the X4 Pro is (TinyUSB CDC, TCF1, boot trial), without a touch panel.
parser.add_argument('--board', choices=('x4pro', 'x4c'), default='x4pro')
args = parser.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
source = (args.main_source or repo / 'src/main.cpp').read_text()
# The #if around the trial's start, whatever it names, through its #endif.
call = source.index('boot_trial::begin();')
begin = source[source.rindex('\n#if', 0, call) + 1:]
begin = begin[:begin.index('#endif') + len('#endif')]
route_start = source.index('  if (recoveryFirmwareMode) {\n    // Skip normal home/reader routing:')
route_end = source.index('\n  if (resume == BootResume::Silent) {\n    // Block until', route_start)
accept_start = source.index('#ifndef SIMULATOR\n  esp_ota_img_states_t otaState;')
accept_end = source.index('\n  allowSleepAt =', accept_start)
def function_of(signature):
    start = source.index(signature)
    depth = 0
    for i in range(source.index('{', start), len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if depth == 0:
            return source[start:i + 1] + '\n'
# The silent boot's own routing helpers (a Settings tab to come back to, a task over it, Join Network's book).
(out / 'helpers.inc').write_text(''.join(function_of(f) for f in (
    'static void openJoinNetwork(', 'static void openSettingsAt(', 'static void openFreshBootTask(')))
(out / 'adapter.inc').write_text(begin + '\n' + source[route_start:route_end] + '\n' + source[accept_start:accept_end])
binary = out / 'boot-trial'
boot_source = args.boot_source or repo / 'src/platform/BootTrial.cpp'
subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-DTENOR_CABLE_FLASH=1',
                '-DFREEINK_DEVICE_X4PRO=1' if args.board == 'x4pro' else '-DFREEINK_DEVICE_X4CLASSIC=1',
                '-I' + str(here / 'stubs'), '-I' + str(boot_source.parent), '-I' + str(repo / 'src/platform'), '-I' + str(repo / 'src'),
                '-I' + str(out), str(here / 'BootTrial.cpp'), str(boot_source),
                '-o', str(binary)], check=True)
cases = ['valid', 'new', 'aborted', 'query-error', 'pass', 'recovery', 'no-touch', 'mark-error',
         'create-error', 'start-error', 'hung-paint', 'valid-race', 'silent-large-book',
         'wake-large-book', 'valid-wake-book', 'silent-transfer', 'silent-fonts',
         'silent-task-clock', 'silent-task-login', 'silent-task-signup', 'silent-task-over-settings',
         'silent-settings-place', 'silent-settings-first-tab', 'silent-join-book', 'silent-join-home', 'no-port',
         'small-rx']
if args.board == 'x4c':
    # No touch panel: its self-check is the first paint and the cable port (CDC begun, its receive buffer).
    cases = ['valid', 'new', 'aborted', 'query-error', 'pass', 'recovery', 'no-touch-x4c', 'no-port', 'small-rx',
             'mark-error', 'create-error', 'start-error', 'hung-paint', 'valid-race']
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
