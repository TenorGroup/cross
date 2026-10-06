"""Compile the actual band routing of EpubReaderActivity::loop with spies.

Touch shell page: the top band opens the top menu, the foot band the text menu (the toolbar's Text panel,
or the list menu when the toolbar style is off); any other zone falls through to the turn and menu taps.
"""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
source = (repo / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
start = source.index('  // Touch shell: the top band opens the top menu')
end = source.index('  if (confirmReleased || ReaderUtils::isTouchMenuGesture(renderer, mappedInput, tenorchrome::kTouchShell)) {', start)
block = source[start:end]
fixture = r'''
#include <cassert>
#include <memory>
#include "''' + str(repo / 'src/activities/reader/ReaderTapZones.h') + r'''"
namespace tenorchrome { constexpr bool kTouchShell = true; }
struct R {}; struct I {};
readertap::Zone next = readertap::Zone::None;
namespace ReaderUtils {
readertap::Zone tapZone(const R&, const I&, bool rtl, bool bands) { assert(!rtl && bands); return next; }
}
struct AM { int topMenus = 0; void openTopMenu() { ++topMenus; } } activityManager;
enum class Overlay { None, Toolbar, Contents, Text, More };
struct Reader {
  R renderer; I mappedInput; bool toolbar = true; std::unique_ptr<int> section = std::make_unique<int>(1);
  int focusedTool = 0, listMenus = 0, fellThrough = 0; Overlay overlay = Overlay::None;
  bool usesToolbarMenu() const { return toolbar; }
  void openOverlay(Overlay o) { overlay = o; }
  void openReaderMenu() { ++listMenus; }
  void loop() {
''' + block + r'''    ++fellThrough;
  }
};
int main() {
  Reader r;
  next = readertap::Zone::TopMenu; r.loop();
  assert(activityManager.topMenus == 1 && r.overlay == Overlay::None && r.fellThrough == 0);
  next = readertap::Zone::TextMenu; r.loop();
  assert(r.overlay == Overlay::Text && r.focusedTool == 1 && r.fellThrough == 0);
  r.overlay = Overlay::None; r.toolbar = false; r.loop();
  assert(r.listMenus == 1 && r.overlay == Overlay::None);
  r.toolbar = true; r.section.reset(); r.loop();
  assert(r.listMenus == 2);
  for (auto z : {readertap::Zone::None, readertap::Zone::Prev, readertap::Zone::Next, readertap::Zone::Menu}) {
    next = z; r.loop();
  }
  assert(r.fellThrough == 4 && activityManager.topMenus == 1 && r.listMenus == 2);
}
'''
with tempfile.TemporaryDirectory(prefix='reader-bands-') as tmp:
    cpp = Path(tmp) / 'bands.cpp'
    exe = Path(tmp) / 'bands'
    cpp.write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('GREEN: reader top band opens the top menu, foot band the text menu, other zones fall through')
