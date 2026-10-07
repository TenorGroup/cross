#!/usr/bin/env python3
"""Run the production logger with a port whose connection flag flaps."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--source', type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
source = args.source or repo / 'lib/Logging/Logging.cpp'
variants = {
    'cdc': ['-DARDUINO_USB_CDC_ON_BOOT=1', '-DARDUINO_USB_MODE=1'],
    'uart': [],
    'rom': ['-DFREEINK_LOG_TRANSPORT=1'],
    'cdc-probe': ['-DARDUINO_USB_CDC_ON_BOOT=1', '-DARDUINO_USB_MODE=1', '-DTENOR_PRESS_PROBE'],
}
failed = 0
for name, flags in variants.items():
    binary = out / name
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', *flags,
                    '-I' + str(here / 'stubs'), '-I' + str(repo / 'lib/Logging'),
                    '-include', str(here / 'stubs/BoardConfig.h'),
                    str(source), str(here / 'LoggingLowClock.cpp'), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    log = result.stdout + result.stderr
    (out / (name + '.log')).write_text(log)
    print(name, 'PASS' if result.returncode == 0 else 'FAIL', log.strip())
    failed += result.returncode != 0
print(f'{len(variants) - failed}/{len(variants)} transport variants passed')
raise SystemExit(bool(failed))
