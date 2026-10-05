"""Compile the actual EpubReaderActivity::handleTapTip with spies.

The button hides the tip for good with one deferred settings write; any other tap, a swipe, Back or
Confirm closes it for this open only, with a full refresh and no page turn; no input leaves it up.
"""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
source = (repo / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
start = source.index('bool EpubReaderActivity::handleTapTip() {')
end = source.index('\n}\n', start) + 3
body = source[start:end]
fixture = r'''
#include <cassert>
#include "''' + str(repo / 'src/activities/reader/ReaderTapZones.h') + r'''"
struct RenderLock { RenderLock() {} };
namespace readertip {
bool shown = true; readertap::Rules r{true, true, false, true, true, 2};
void close() { shown = false; }
const readertap::Rules& rules() { return r; }
}
struct Settings { int readerTapTip = 1; } SETTINGS;
int deferred = 0;
struct AM { void deferWrite(void (*)()) { ++deferred; } } activityManager;
void saveTapTipHidden() {}
struct MappedInputManager {
  enum class SwipeDir { None, Left, Right, Up, Down };
  enum class Button { Back, Confirm };
  bool tap = false; int x = 0, y = 0; SwipeDir swipe = SwipeDir::None; bool back = false, confirm = false;
  bool wasScreenTapped(int& tx, int& ty) const { tx = x; ty = y; return tap; }
  SwipeDir wasSwipe() const { return swipe; }
  bool wasReleased(Button b) const { return b == Button::Back ? back : confirm; }
};
struct R { int getScreenWidth() const { return 480; } int getScreenHeight() const { return 800; } };
struct EpubReaderActivity {
  R renderer; MappedInputManager mappedInput; int pagesUntilFullRefresh = 9, updates = 0;
  void requestUpdate() { ++updates; }
  bool handleTapTip();
};
'''
test = r'''
int main() {
  EpubReaderActivity a;
  // Nothing this pass: the tip stays, the pass is still the tip's.
  assert(a.handleTapTip() && readertip::shown && a.updates == 0);
  // A tap on the page (the forward zone): closed, not hidden, full refresh.
  a.mappedInput.tap = true; a.mappedInput.x = 300; a.mappedInput.y = 200;
  assert(a.handleTapTip() && !readertip::shown && SETTINGS.readerTapTip == 1 && deferred == 0);
  assert(a.pagesUntilFullRefresh == 9 && a.updates == 1);  // a page turn's refresh, no forced full one
  // A swipe, Back, Confirm: closed, not hidden.
  for (int i = 0; i < 3; ++i) {
    readertip::shown = true;
    a.mappedInput = {};
    if (i == 0) a.mappedInput.swipe = MappedInputManager::SwipeDir::Left;
    if (i == 1) a.mappedInput.back = true;
    if (i == 2) a.mappedInput.confirm = true;
    assert(a.handleTapTip() && !readertip::shown && SETTINGS.readerTapTip == 1 && deferred == 0);
  }
  // The button: hidden for good, one deferred write.
  readertip::shown = true;
  const auto b = readertap::tipButton(480, 800, readertip::r);
  a.mappedInput = {}; a.mappedInput.tap = true; a.mappedInput.x = b.x + 5; a.mappedInput.y = b.y + 5;
  assert(a.handleTapTip() && !readertip::shown && SETTINGS.readerTapTip == 0 && deferred == 1);
}
'''
with tempfile.TemporaryDirectory(prefix='tip-input-') as tmp:
    cpp = Path(tmp) / 'tip.cpp'
    exe = Path(tmp) / 'tip'
    cpp.write_text(fixture + body + test)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('GREEN: tip button hides it with one deferred write; tap, swipe, Back, Confirm only close it')
