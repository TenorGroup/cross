#!/usr/bin/env python3
"""The update boot: the production screen steps and restart around the RTC flag, the flag's
callers in the sources, and the TLS record slot. Release and probe builds of the screen."""
import argparse
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cxx', default='c++')
a = p.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)


def block(source, marker):
    """The marker line and its brace-matched body."""
    start = source.index(marker)
    at = source.index('{', start + len(marker) - 1) + 1
    depth = 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[start:at] + '\n'


screen = (repo / 'src/activities/settings/OtaUpdateActivity.cpp').read_text()
main = (repo / 'src/main.cpp').read_text()
header = (repo / 'src/SilentRestart.h').read_text()
restart = block(main, 'void silentRestartToOta(')
steps = [block(screen, 'void OtaUpdateActivity::' + f + '(') for f in
         ('onWifiSelectionComplete', 'onExit', 'runUpdateInstall', 'render')]
dry = block(screen, 'void OtaUpdateActivity::runDryRun(')
(out / 'production-restart.inc').write_text(
    re.search(r'void silentRestartToOta\([^;]*\);', header).group(0) + '\n' + restart)
(out / 'production-screen.inc').write_text(''.join(steps) + '#ifdef TENOR_PRESS_PROBE\n' + dry + '#endif\n')

# Who may arm the update boot: the user's Update choice and the probe's dry-run command, nobody else.
problems = []
for path in sorted((repo / 'src').rglob('*')):
    if path.suffix not in ('.cpp', '.h') or path.name == 'UpdateBoot.h':
        continue
    text = path.read_text(errors='replace')
    rel = path.relative_to(repo).as_posix()
    def code(pattern):
        # Matches outside // comments.
        return [m.start() for m in re.finditer(pattern, text)
                if '//' not in text[text.rfind('\n', 0, m.start()) + 1:m.start()]]
    calls = code(r'silentRestartToOta\(')
    arms = code(r'update_boot::arm\(')
    if rel == 'src/SilentRestart.h':
        continue
    if rel == 'src/main.cpp':
        definition = text.index('void silentRestartToOta(') + len('void ')
        handler = text.index('cmd.startsWith("OTA_DRYRUN ")')
        handler_end = text.index('} else if (cmd', handler)
        probe_open = text.rfind('#ifdef TENOR_PRESS_PROBE', 0, handler)
        probe_close = text.rfind('#endif', 0, handler)
        for at in calls:
            if at != definition and not (handler < at < handler_end and probe_open > probe_close):
                problems.append(f'{rel}: silentRestartToOta outside the probe dry-run command')
        for at in arms:
            if not definition < at < definition + len(restart):
                problems.append(f'{rel}: update_boot::arm outside silentRestartToOta')
        take = text.find('update_boot::take(')
        setup = text.index('void setup() {')
        if take < 0 or not setup < take < text.index('gpio.begin();', setup):
            problems.append(f'{rel}: update_boot::take is not spent first thing in setup()')
        continue
    if arms:
        problems.append(f'{rel}: update_boot::arm outside main.cpp')
    if rel == 'src/activities/settings/OtaUpdateActivity.cpp':
        on_exit = text.index('void OtaUpdateActivity::onExit(')
        on_exit_end = on_exit + len(block(text, 'void OtaUpdateActivity::onExit('))
        if any(not on_exit < at < on_exit_end for at in calls):
            problems.append(f'{rel}: silentRestartToOta outside onExit')
        sets = code(r'restartIntoInstall = ')
        popup = text.index('confirmPopup.show(')
        popup_end = text.index('});', popup)
        if len(sets) != 1 or not popup < sets[0] < popup_end:
            problems.append(f'{rel}: restartIntoInstall set outside the Update choice')
    elif calls:
        problems.append(f'{rel}: silentRestartToOta called')
for problem in problems:
    print('FAIL callers:', problem)
if not problems:
    print('PASS callers')

flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
         '-DCROSSPOINT_EMULATED=0', '-DFREEINK_DEVICE_X3=1', '-DCROSSPOINT_VERSION="1.0.16"',
         '-I' + str(here / 'stubs'), '-I' + str(repo / 'test/ota_back_cancel/stubs'),
         '-I' + str(repo / 'test/file_transfer_back_latch/stubs'),
         '-I' + str(repo / 'src/network'), '-I' + str(repo / 'lib/NetworkTrust'), '-I' + str(out)]
sources = [str(here / 'OtaCleanBoot.cpp'), str(repo / 'src/network/OtaUpdater.cpp'),
           str(repo / 'src/network/FirmwareBoardTag.cpp'), str(repo / 'src/network/TlsRecordSlot.cpp')]
binaries = {}
for build, extra in (('release', []), ('probe', ['-DTENOR_PRESS_PROBE'])):
    binaries[build] = out / f'ota-clean-boot-{build}'
    subprocess.run([a.cxx, *flags, *extra, *sources, '-o', str(binaries[build])], check=True)
cases = [('release', 'flag', 'normal-boot-is-not-an-update-boot'), ('release', 'flag', 'armed-flag-is-spent-once'),
         ('release', 'flag', 'power-loss-leaves-no-update-boot'), ('release', 'screen', 'check-alone-arms-nothing'),
         ('release', 'screen', 'update-restarts-into-install'), ('release', 'screen', 'cancel-restarts-home'),
         ('release', 'screen', 'update-boot-installs-without-asking'),
         ('release', 'screen', 'update-boot-failure-restarts-home'),
         ('release', 'screen', 'update-boot-without-wifi-restarts-home'),
         ('probe', 'screen', 'dry-run-series-one-run-a-boot'), ('release', 'screen', 'install-comes-in-parts'),
         ('release', 'screen', 'broken-part-asks-again-from-reached-byte'),
         ('release', 'screen', 'broken-parts-run-out'), ('release', 'screen', 'server-without-ranges-sends-whole-image'),
         ('release', 'screen', 'range-ignored-after-first-part-fails'),
         ('release', 'screen', 'progress-frames-allocate-nothing'),
         ('probe', 'screen', 'progress-frames-allocate-nothing'), ('release', 'slot', 'slot-takes-the-record-size-when-short'), ('release', 'slot', 'slot-one-block-for-every-record'),
         ('release', 'slot', 'slot-leaves-other-requests-alone'), ('release', 'slot', 'slot-without-memory-falls-back')]
results = [{'case': 'callers', 'exit': int(bool(problems)), 'log': '\n'.join(problems)}]
for build, group, name in cases:
    run = subprocess.run([str(binaries[build]), group, name], capture_output=True, text=True)
    results.append({'case': name, 'exit': run.returncode, 'log': run.stdout + run.stderr})
    print(run.stdout + run.stderr, end='')
(out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
print(f"{sum(r['exit'] == 0 for r in results)}/{len(results)} passed")
raise SystemExit(int(any(r['exit'] for r in results)))
