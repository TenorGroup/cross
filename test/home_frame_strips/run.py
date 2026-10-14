"""The X3 Large cover and Shelf frames go to the card and come back in strips, with a heap whose largest block is 28.660 B.

Pulls the real functions out of HomeActivity.cpp (the way test/home_thumb_work does) and runs them on a stub screen buffer, a
stub card and a heap that refuses any block over 28.660 B. --source runs another copy of HomeActivity.cpp (the red run).
"""
import argparse
import pathlib
import subprocess

here = pathlib.Path(__file__).resolve().parent
repo = here.parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--output', type=pathlib.Path, required=True)
ap.add_argument('--largest', type=int, default=28660, help='largest block the stub heap gives (a control run uses a big one)')
ap.add_argument('--source', type=pathlib.Path, default=repo / 'src/activities/home/HomeActivity.cpp')
a = ap.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
t = a.source.read_text()


def function(signature):
    start = t.index(signature)
    brace = t.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (t[end] == '{') - (t[end] == '}')
        end += 1
    return t[start:end] + '\n'


strips = repo / 'src/activities/home/FrameStrips.h'
head = t[t.index('struct CardFileHead {'):t.index('// Books whose missing card thumbnail')]
source = (here / 'harness.h').read_text()
if strips.exists():
    source += '#include "FrameStrips.h"\n'
source += head
source += ''.join(function(s) for s in (
    'bool HomeActivity::frameOnCard() const', 'bool HomeActivity::storeCoverBuffer()', 'bool HomeActivity::restoreCoverBuffer()',
    'void HomeActivity::freeCoverBuffer()', 'void HomeActivity::holdFrame(', 'bool HomeActivity::restoreFrame(',
    'HomeActivity::CardFile HomeActivity::loadCardFile(', 'void HomeActivity::saveCardFile()'))
source += (here / 'cases.cpp').read_text()
p = a.output / 'generated.cpp'
p.write_text(source)
subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wno-unused-const-variable', '-fsanitize=address,undefined',
                '-DLARGEST=%d' % a.largest, '-I', str(here / 'stubs'), '-I', str(repo / 'src/activities/home'),
                '-I', str(repo / 'freeink-sdk/libs/hardware/SDCardManager/include'), str(p), '-o', str(a.output / 'test')], check=True)
r = subprocess.run([str(a.output / 'test')], capture_output=True, text=True)
print(r.stdout, end='')
print(r.stderr, end='')
(a.output / 'result.log').write_text(r.stdout + r.stderr)
raise SystemExit(r.returncode)
