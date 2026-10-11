#!/usr/bin/env python3
"""Run the production one-bit painter with packed rows and count column rounding."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cxx', default='c++')
a = p.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
a.output.mkdir(parents=True, exist_ok=True)
s = (repo / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
start = s.index('bool GfxRenderer::drawBitmap1Bit(')
end = s.index('\nvoid GfxRenderer::preserveImagePolarity', start)
(a.output / 'paint.inc').write_text(s[start:end].replace('std::floor(', 'countedFloor('))
binary = a.output / 'bitmap-columns'
subprocess.run([a.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I' + str(a.output), '-I' + str(repo / 'lib/Memory'),
                str(here / 'paint.cpp'), '-o', str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
