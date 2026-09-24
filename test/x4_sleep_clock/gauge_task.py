#!/usr/bin/env python3
"""Compile gauge_task.cpp with the production battery and charging reads.

HalPowerManager.cpp is compiled whole. HalGPIO::isUsbConnected() and the powerManager
call the main loop makes on every pass are taken verbatim from lib/hal/HalGPIO.cpp and
src/main.cpp, so the check follows the code that ships.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess


def body(source, start):
    index = source.index('{', start)
    depth = 0
    for position in range(index, len(source)):
        depth += (source[position] == '{') - (source[position] == '}')
        if depth == 0:
            return source[start:position + 1]
    raise SystemExit('unclosed function')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = (args.source_root or here.parents[1]).resolve()
    production = args.output.resolve() / 'production'
    production.mkdir(parents=True, exist_ok=True)
    for name in ('HalPowerManager.cpp', 'HalPowerManager.h'):
        shutil.copyfile(repo / 'lib/hal' / name, production / name)
    gpio = (repo / 'lib/hal/HalGPIO.cpp').read_text()
    (production / 'usb.inc').write_text(body(gpio, gpio.index('bool HalGPIO::isUsbConnected() const')) + '\n')
    main_source = (repo / 'src/main.cpp').read_text()
    loop = body(main_source, main_source.index('void loop() {'))
    polls = re.findall(r'(?m)^\s*powerManager\.(\w+)\(\);\s*$', loop)
    if len(polls) != 1:
        raise SystemExit(f'expected one powerManager poll in loop(), found {polls}')
    sdk = repo / 'freeink-sdk/libs/hardware/PowerManager'
    binary = production.parent / 'gauge-task'
    cmd = [args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
           '-fno-omit-frame-pointer', '-DSOC_PM_SUPPORT_EXT1_WAKEUP=0', f'-DLOOP_POLL={polls[0]}',
           '-I' + str(here / 'stubs'), '-I' + str(production), '-I' + str(here), '-I' + str(sdk / 'include'),
           str(production / 'HalPowerManager.cpp'), str(sdk / 'src/PowerManager.cpp'),
           str(here / 'gauge_task.cpp'), '-o', str(binary)]
    subprocess.run(cmd, check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    print(f'loop poll: powerManager.{polls[0]}()')
    print(run.stdout + run.stderr, end='')
    return run.returncode


if __name__ == '__main__':
    raise SystemExit(main())
