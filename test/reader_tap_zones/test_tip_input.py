"""Compile the actual EpubReaderActivity::handleTapTip with spies.

The button hides the tip for good with one deferred settings write; any other tap, a swipe, Back or
Confirm closes it for this open only, with a full refresh and no page turn; no input leaves it up. A side
page key closes it too but is not swallowed: the pass goes on to turn the page.
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
struct CrossPointSettings { enum { OFF, CHAPTER_SKIP, SIDE_HOLD_BACK = 4 }; };
struct Settings { int readerTapTip = 1; int longPressButtonBehavior = 1; } SETTINGS;
int deferred = 0;
struct AM { void deferWrite(void (*)()) { ++deferred; } } activityManager;
void saveTapTipHidden() {}
struct MappedInputManager {
  enum class SwipeDir { None, Left, Right, Up, Down };
  enum class Button { Back, Confirm, PageForward, PageBack };
  bool tap = false; int x = 0, y = 0; SwipeDir swipe = SwipeDir::None; bool back = false, confirm = false;
  bool pagePress = false, pageRelease = false;  // a side page key goes down / comes up this pass
  bool wasPressed(Button b) const { return pagePress && b == Button::PageForward; }
  bool wasScreenTapped(int& tx, int& ty) const { tx = x; ty = y; return tap; }
  SwipeDir wasSwipe() const { return swipe; }
  bool wasReleased(Button b) const {
    return b == Button::Back ? back : b == Button::Confirm ? confirm : pageRelease && b == Button::PageForward;
  }
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
  assert(a.pagesUntilFullRefresh == 1 && a.updates == 1);  // the page comes back with 1 full refresh
  // A swipe, Back, Confirm: closed, not hidden.
  for (int i = 0; i < 3; ++i) {
    readertip::shown = true;
    a.mappedInput = {};
    if (i == 0) a.mappedInput.swipe = MappedInputManager::SwipeDir::Left;
    if (i == 1) a.mappedInput.back = true;
    if (i == 2) a.mappedInput.confirm = true;
    assert(a.handleTapTip() && !readertip::shown && SETTINGS.readerTapTip == 1 && deferred == 0);
  }
  // A side page key: the tip closes for this open (not hidden for good), and the pass is not swallowed, so the
  // page turns. The key counts when it goes down, or when it comes up if a hold means Back.
  for (int i = 0; i < 2; ++i) {
    readertip::shown = true; a.updates = 0; a.pagesUntilFullRefresh = 9; deferred = 0;
    SETTINGS.longPressButtonBehavior = i ? CrossPointSettings::SIDE_HOLD_BACK : 1;
    a.mappedInput = {};
    (i ? a.mappedInput.pageRelease : a.mappedInput.pagePress) = true;
    assert(!a.handleTapTip() && !readertip::shown && SETTINGS.readerTapTip == 1 && deferred == 0);
    assert(a.pagesUntilFullRefresh == 1 && a.updates == 1);
  }
  // A key going down while a hold means Back is not yet a turn: the tip stays.
  readertip::shown = true; a.mappedInput = {}; a.mappedInput.pagePress = true;
  assert(a.handleTapTip() && readertip::shown);
  SETTINGS.longPressButtonBehavior = 1;
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
print('GREEN: tip button hides it with one deferred write; tap, swipe, Back, Confirm only close it; a side page key closes it and turns the page')
