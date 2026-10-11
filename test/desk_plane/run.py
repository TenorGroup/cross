#!/usr/bin/env python3
"""Exercise the touch desk's production background across repaint and allocation refusal."""
import argparse
from pathlib import Path
import subprocess
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cxx', default='c++')
a = p.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[1]
s = (repo / 'src/shells/ugly/UglyDesk.cpp').read_text()
start = s.index('void Desk::render(RenderLock&&) {')
end = s.index('  for (int i = 0; i < COUNT; ++i) {\n    const char* label', start)
a.output.mkdir(parents=True, exist_ok=True)
(a.output / 'background.inc').write_text(s[start:end] + '}\n')
binary = a.output / 'desk-plane'
subprocess.run([a.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I' + str(a.output), str(here / 'plane.cpp'), '-o', str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
