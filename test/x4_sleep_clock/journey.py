#!/usr/bin/env python3
"""Check that every stored wakeButtons value sleeps exactly like power-button-only sleep.

The device sleep helper is extracted verbatim from src/main.cpp and compiled with
the complete production HalPowerManager.cpp and SDK PowerManager.cpp. The
recorded boundary calls for card values 0 to 3 must equal the reference trace in
power-only-trace.json, which was recorded from the b2e808f sources with
wakeButtons=0 (`--record` on that tree).
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def extract(source, pattern):
    match = re.search(pattern, source)
    if not match:
        raise SystemExit(f'missing device sleep helper: {pattern}')
    index = source.index('{', match.end() - 1)
    depth = 0
    for position in range(index, len(source)):
        depth += (source[position] == '{') - (source[position] == '}')
        if depth == 0:
            return match.group(1), source[match.start():position + 1]
    raise SystemExit('unclosed device sleep helper')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--record', type=Path, help='write the card0 traces as a new reference file')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = (args.source_root or here.parents[1]).resolve()
    out = args.output.resolve()
    production = out / 'production'
    production.mkdir(parents=True, exist_ok=True)
    main_source = (repo / 'src/main.cpp').read_text()
    name, helper = extract(main_source, r'static void (sleepWithConfiguredButtons|sleepUntilPowerButton)\(\) \{')
    (production / 'helper.inc').write_text(f'#define DEVICE_SLEEP_HELPER {name}\n{helper}\n')
    hashes = {'src/main.cpp:' + name: hashlib.sha256(helper.encode()).hexdigest()}
    for original in sorted((repo / 'lib/hal').glob('HalPowerManager.*')) + sorted((repo / 'lib/hal').glob('WakeButtons.h')):
        shutil.copyfile(original, production / original.name)
        hashes[str(original.relative_to(repo))] = hashlib.sha256(original.read_bytes()).hexdigest()
    sdk = repo / 'freeink-sdk/libs/hardware/PowerManager'
    binary = out / 'journey-c3'
    cmd = [args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-DENABLE_SERIAL_LOG', '-DSOC_PM_SUPPORT_EXT1_WAKEUP=0',
           '-I' + str(here / 'stubs'), '-I' + str(production), '-I' + str(here), '-I' + str(sdk / 'include'),
           str(production / 'HalPowerManager.cpp'), str(sdk / 'src/PowerManager.cpp'),
           str(here / 'journey.cpp'), '-o', str(binary)]
    subprocess.run(cmd, check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
    measured = json.loads(run.stdout)
    (out / 'journey.json').write_text(json.dumps(measured, indent=2) + '\n')
    (out / 'journey-manifest.json').write_text(json.dumps({'source_root': str(repo), 'source_hashes': hashes,
                                                          'command': cmd}, indent=2) + '\n')
    if args.record:
        reference = {case[:-len('-card0')]: value for case, value in measured.items() if case.endswith('-card0')}
        args.record.write_text(json.dumps(reference, indent=2) + '\n')
        print(f'recorded {len(reference)} power-button-only traces from {repo}')
        return 0
    reference = json.loads((here / 'power-only-trace.json').read_text())
    failures = 0
    for case, value in sorted(measured.items()):
        expected = reference[case.rsplit('-card', 1)[0]]
        if value == expected:
            print(f'PASS {case}: {len(value["trace"])} calls, identical to power-button-only sleep')
            continue
        failures += 1
        got, want = value['trace'], expected['trace']
        first = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
        print(f'FAIL {case}: lightSleeps={value["lightSleeps"]} (want {expected["lightSleeps"]}) '
              f'ms={value["ms"]} (want {expected["ms"]}) calls={len(got)} (want {len(want)}) '
              f'first difference at call {first}: got {got[first:first + 4]} want {want[first:first + 4]}')
    print(f'{len(measured)} cases, {failures} failures')
    return int(failures != 0)


if __name__ == '__main__':
    raise SystemExit(main())
