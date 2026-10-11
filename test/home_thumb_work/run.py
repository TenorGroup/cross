import argparse
import pathlib
import subprocess

here = pathlib.Path(__file__).resolve().parent
repo = here.parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--output', type=pathlib.Path, required=True)
ap.add_argument('--case', default='power')
a = ap.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
t = (repo / 'src/activities/home/HomeActivity.cpp').read_text()

def function(signature):
    start = t.index(signature)
    end = t.index('{', start) + 1
    depth = 1
    while depth:
        depth += (t[end] == '{') - (t[end] == '}')
        end += 1
    return t[start:end] + '\n'

source = (here / 'stubs.h').read_text()
source += t[t.index('uint32_t fnv(uint32_t hash, const void* data'):t.index('\n// Books whose missing card thumbnail')]
source += '\n'.join(function(s) for s in ('void HomeActivity::writeMissingThumb(', 'bool HomeActivity::tickHolders()', 'void HomeActivity::queueTouchThumb('))
source += (here / 'cases.cpp').read_text()
p = a.output / 'home.cpp'
p.write_text(source)
subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', str(p), '-o', str(a.output / 'home')], check=True)
raise SystemExit(subprocess.run([str(a.output / 'home'), a.case]).returncode)
