"""Compile the actual condition that stops the reader's automatic page turn, with spies.

Touch shell: a tap on the foot band (the one menu's way in) stops it, as the centre tap did before the
centre cell went (founder 06/10); a tap that turns a page does not.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
source = (repo / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
start = source.index('  if (automaticPageTurnActive) {\n    if (') + len('  if (automaticPageTurnActive) {\n    if (')
end = source.index(') {\n      automaticPageTurnActive = false;', start)
condition = source[start:end]
fixture = r'''
#include <cassert>
#include "''' + str(Path(__file__).resolve().parents[2] / 'src/activities/reader/ReaderTapZones.h') + r'''"
namespace tenorchrome { constexpr bool kTouchShell = true; }
struct R {};
struct MappedInputManager {
  enum class Button { Back, Confirm };
  bool wasReleased(Button) const { return false; }
};
readertap::Zone zone = readertap::Zone::None;
namespace ReaderUtils {
bool isTouchMenuGesture(const R&, const MappedInputManager&, bool = false) { return false; }
readertap::Zone tapZone(const R&, const MappedInputManager&, bool, bool) { return zone; }
}
bool stops() {
  R renderer;
  MappedInputManager mappedInput;
  return ''' + condition + r''';
}
int main() {
  zone = readertap::Zone::TextMenu;
  assert(stops());
  for (auto z : {readertap::Zone::None, readertap::Zone::Next, readertap::Zone::Prev}) {
    zone = z;
    assert(!stops());
  }
}
'''
with tempfile.TemporaryDirectory(prefix='auto-turn-stop-') as tmp:
    cpp = Path(tmp) / 'stop.cpp'
    exe = Path(tmp) / 'stop'
    cpp.write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('GREEN: a foot band tap stops the automatic page turn, a turning tap does not')
