#!/usr/bin/env python3
"""Exercise production navigation and measure its actual callback captures."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
source = (repo / 'src/activities/library/LibraryListActivity.cpp').read_text()
start = source.index('void LibraryListActivity::navigateButtons() {')
end = source.index('\nvoid LibraryListActivity::buildRows(', start)
(out / 'navigation.inc').write_text(source[start:end])
binary = out / 'navigation'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-I' + str(out), str(here / 'NavCapture.cpp'), '-o', str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
