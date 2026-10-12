import argparse
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('--output', type=Path, required=True)
ap.add_argument('--case', choices=['refresh', 'ghost'], default='refresh')
a = ap.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
exe = a.output / a.case
subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                '-I' + str(here.parents[1]), str(here / (a.case + '.cpp')), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
