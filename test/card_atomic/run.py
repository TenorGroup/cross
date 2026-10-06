import argparse
import pathlib
import subprocess

p = pathlib.Path(__file__).resolve().parent
repo = p.parents[1]
a = argparse.ArgumentParser()
a.add_argument('--source', type=pathlib.Path, default=repo / 'src/activities/home/HomeActivity.cpp')
a.add_argument('--output', type=pathlib.Path, required=True)
args = a.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
t = args.source.read_text()
def function(signature):
    start = t.index(signature)
    brace = t.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (t[end] == '{') - (t[end] == '}')
        end += 1
    return t[start:end] + '\n'
head = t[t.index('struct CardFileHead {'):t.index('\nuint32_t fnv(')]
source = (p / 'harness.h').read_text() + head + function('HomeActivity::CardFile HomeActivity::loadCardFile(') + function('void HomeActivity::saveCardFile()') + (p / 'cases.cpp').read_text()
(args.output / 'generated.cpp').write_text(source)
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-I', str(repo / 'freeink-sdk/libs/hardware/SDCardManager/include'), str(args.output / 'generated.cpp'), '-o', str(args.output / 'test')], check=True)
r = subprocess.run([str(args.output / 'test')], capture_output=True, text=True)
print(r.stdout, end='')
print(r.stderr, end='')
(args.output / 'result.log').write_text(r.stdout + r.stderr)
raise SystemExit(r.returncode)
