#!/usr/bin/env python3
"""Count the paints of the first screen after a silent restart, through production code."""
import argparse
from pathlib import Path
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
    at = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[start:at] + '\n'


manager = (repo / 'src/activities/ActivityManager.cpp').read_text()
functions = ['void ActivityManager::renderTaskLoop() {', 'void ActivityManager::requestUpdate(bool immediate) {',
             'void ActivityManager::requestUpdateAndWait() {']
first_paint = 'void ActivityManager::requestFirstPaintAndWait() {'
has_first_paint = first_paint in manager
if has_first_paint:
    functions.append(first_paint)
(out / 'production-manager.inc').write_text(''.join(block(manager, f) for f in functions))
loop = block(manager, 'void ActivityManager::loop() {')
(out / 'production-flush.inc').write_text(block(loop, '  if (requestedUpdate.exchange(false)) {\n    // Using direct'))
main = (repo / 'src/main.cpp').read_text()
(out / 'production-silent.inc').write_text(
    block(main, '  if (resume == BootResume::Silent) {\n    // Block until the first paint'))
binary = out / 'silent-boot-paint'
subprocess.run([a.cxx, '-std=c++17', '-pthread', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', f'-DHAS_FIRST_PAINT={int(has_first_paint)}',
                '-I' + str(out), str(here / 'SilentBootPaint.cpp'), '-o', str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
