#!/usr/bin/env python3
"""Check the viewer exit delegates the next frame to its destination."""
import argparse
from pathlib import Path
import subprocess
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--cxx', default='c++')
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
s = (repo / 'src/activities/util/BmpViewerActivity.cpp').read_text()
start = s.index('void BmpViewerActivity::onExit()')
end = s.index('\nvoid BmpViewerActivity::doSetSleepCover()', start)
a.output.mkdir(parents=True, exist_ok=True)
source = a.output / 'exit.cpp'
source.write_text('''#include <cstdio>
struct HalDisplay { enum { HALF_REFRESH }; };
struct Activity { int exits = 0; void onExit() { ++exits; } };
struct Renderer {
  int clears = 0, pushes = 0;
  void clearScreen() { ++clears; }
  void displayBuffer(int) { ++pushes; }
};
struct BmpViewerActivity : Activity { Renderer renderer; void onExit(); };
''' + s[start:end] + '''
int main() {
  BmpViewerActivity view;
  view.onExit();
  std::printf("exit=%d clear=%d panel=%d\\n", view.exits, view.renderer.clears, view.renderer.pushes);
  return view.exits != 1 || view.renderer.clears != 0 || view.renderer.pushes != 0;
}
''')
binary = a.output / 'exit'
subprocess.run([a.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
