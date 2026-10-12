import argparse
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
ap = argparse.ArgumentParser()
ap.add_argument('--output', type=Path, required=True)
a = ap.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
exe = a.output / 'refresh'
subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                '-I' + str(here.parents[1]), str(here / 'refresh.cpp'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
