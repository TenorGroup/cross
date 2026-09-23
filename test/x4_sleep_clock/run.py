#!/usr/bin/env python3
"""Compile complete production HAL and SDK sleep paths against GPIO/IDF boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = (args.source_root or here.parents[1]).resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    production = out / 'production'
    production.mkdir(exist_ok=True)
    hashes = {}
    for name in ('HalPowerManager.cpp', 'HalPowerManager.h'):
        original = repo / 'lib/hal' / name
        shutil.copyfile(original, production / name)
        hashes[str(original)] = hashlib.sha256(original.read_bytes()).hexdigest()
    sdk = repo / 'freeink-sdk/libs/hardware/PowerManager'
    for name in ('src/PowerManager.cpp', 'include/PowerManager.h'):
        hashes[str(sdk / name)] = hashlib.sha256((sdk / name).read_bytes()).hexdigest()
    has_arg = 'preserveClock' in (production / 'HalPowerManager.h').read_text()
    runs = []
    failed = False
    for soc, ext1 in (('c3', 0), ('s3', 1)):
        binary = out / f'sleep-{soc}'
        cmd = [args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-DENABLE_SERIAL_LOG', f'-DSOC_PM_SUPPORT_EXT1_WAKEUP={ext1}',
               f'-DSLEEP_HAS_PRESERVE_ARG={int(has_arg)}', '-I' + str(here / 'stubs'),
               '-I' + str(production), '-I' + str(sdk / 'include'),
               str(production / 'HalPowerManager.cpp'), str(sdk / 'src/PowerManager.cpp'),
               str(here / 'behavior.cpp'), '-o', str(binary)]
        subprocess.run(cmd, check=True)
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        (out / f'{soc}.log').write_text(run.stdout + run.stderr)
        runs.append({'soc': soc, 'command': cmd, 'exit_code': run.returncode,
                     'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()})
        print(soc, run.stdout.strip())
        failed |= run.returncode != 0
    (out / 'manifest.json').write_text(json.dumps({'source_root': str(repo), 'source_hashes': hashes,
                                                  'runs': runs}, indent=2) + '\n')
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
