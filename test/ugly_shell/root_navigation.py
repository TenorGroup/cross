import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
settings = (args.repo / 'src/CrossPointSettings.h').read_text()
assert 'migrateUglyRoot(' in settings, 'RED: legacy startup has no root migration'
fields = settings[settings.index('  enum UGLY_START_SCREEN'):settings.index('  // Whether a power-button wake')]
program = '''#include <cassert>
#include <cstdint>
#include <cstdio>
struct CrossPointSettings { FIELDS };
int main() {
  int cases = 0;
  for (bool hasRoot : {false, true})
    for (int root = 0; root < 2; ++root)
      for (int start = 0; start < 5; ++start) {
        CrossPointSettings value;
        value.uglyRootScreen = root;
        value.uglyStartScreen = start;
        value.migrateUglyRoot(hasRoot, start);
        const int expectedRoot = !hasRoot && start == 3 ? 1 : !hasRoot && start == 1 ? 0 : root;
        assert(value.uglyRootIsDesk() == (expectedRoot == 1));
        assert(value.startScreen(true) == (start == 0 ? 0 : start == 2 ? 2 : 1));
        assert(value.startScreen(false) == 1);
        ++cases;
      }
  std::printf("GREEN: %d root/startup/migration combinations\\n", cases);
}
'''.replace('FIELDS', fields).replace('#include <cassert>', '#include <cassert>\n#include <initializer_list>')
source = args.output / 'root.cpp'
source.write_text(program)
binary = args.output / 'root'
subprocess.run(['c++', '-std=c++17', str(source), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
shell = (args.repo / 'src/shells/ugly/UglyShell.cpp').read_text()
desk = (args.repo / 'src/shells/ugly/UglyDesk.cpp').read_text()
header = (args.repo / 'src/shells/ugly/UglyDesk.h').read_text()
browser = (args.repo / 'src/activities/home/FileBrowserActivity.cpp').read_text()
notebook = (args.repo / 'src/shells/ugly/UglyNotebook.cpp').read_text()
assert 'return makeRoot(renderer, mappedInput, cleanInitialRefresh);' in shell
assert 'if (!SETTINGS.uglyRootIsDesk())' in desk
assert 'acceptsTiltTabNavigation() const override { return true; }' in header
assert 'openedFromUglyFile' in browser and 'FileBrowserActivity::Mode::Books,\n              true' in notebook
print('GREEN: root exit, desk back, horizontal tilt and File source binding')
