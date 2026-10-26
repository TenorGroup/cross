"""Compile the actual ReaderUtils::detectPageTurn with a scripted input.

"Giu nut lat trang khi doc" = side buttons, previous page (SIDE_HOLD_PREV, stored 5): a side key turns on its press
(sideTurn says which way), and its hold is sidePrev, never a chapter skip; the front keys and SIDE_HOLD_BACK keep
what they did.
"""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
source = (repo / 'src/activities/reader/ReaderUtils.h').read_text()
start = source.index('struct PageTurnResult {')
end = source.index('struct TouchPageTurn {', start)
body = source[start:end]
fixture = r'''
#include <cassert>
#include <cstdint>
struct CrossPointSettings {
  enum { OFF = 0, CHAPTER_SKIP = 1, ORIENTATION_CHANGE = 2, SIDE_HOLD_BACK = 4, SIDE_HOLD_PREV = 5 };
  enum SHORT_PWRBTN { IGNORE = 0, PAGE_TURN = 2 };
};
struct Settings {
  enum { OFF = 0 };
  int longPressButtonBehavior = 1, tiltPageTurn = 0, shortPwrBtn = 0;
} SETTINGS;
struct Tilt { bool wasTiltedForward() const { return false; } bool wasTiltedBack() const { return false; } } halTiltSensor;
enum class HomeButtonAction { Ignore, NextPage };
struct MappedInputManager {
  enum class Button { PageBack, PageForward, Left, Right, Power };
  int down = -1, up = -1, held = -1;  // the key that goes down, comes up, or has been held 700 ms this pass
  mutable bool heldAsked = false;     // a long press is a one-shot
  bool wasPressed(Button b) const { return down == static_cast<int>(b); }
  bool wasReleased(Button b) const { return up == static_cast<int>(b); }
  bool wasLongPressed(Button b, unsigned long) const {
    if (heldAsked || held != static_cast<int>(b)) return false;
    heldAsked = true;
    return true;
  }
  bool isNavDirectionSwapped() const { return false; }
  HomeButtonAction homeButtonAction() const { return HomeButtonAction::Ignore; }
};
namespace ReaderUtils {
constexpr unsigned long SKIP_HOLD_MS = 700;
'''
test = r'''
}  // namespace ReaderUtils
using B = MappedInputManager::Button;
ReaderUtils::PageTurnResult pass(int down, int up, int held) {
  MappedInputManager input;
  input.down = down; input.up = up; input.held = held;
  return ReaderUtils::detectPageTurn(input);
}
int main() {
  const int fwd = static_cast<int>(B::PageForward), back = static_cast<int>(B::PageBack);
  const int right = static_cast<int>(B::Right);
  SETTINGS.longPressButtonBehavior = CrossPointSettings::SIDE_HOLD_PREV;
  // The side key turns on its press, and says which way.
  auto r = pass(fwd, -1, -1);
  assert(r.next && r.nextButtonPressed && r.sideTurn == 1 && !r.sidePrev);
  r = pass(back, -1, -1);
  assert(r.prev && r.sideTurn == -1);
  // Its release turns nothing.
  r = pass(-1, fwd, -1);
  assert(!r.next && !r.prev && r.sideTurn == 0);
  // Held, either side key is sidePrev: not a chapter skip, not Back.
  r = pass(-1, -1, fwd);
  assert(r.sidePrev && !r.sideBack && !r.nextLongPressed && !r.prevLongPressed && !r.next);
  r = pass(-1, -1, back);
  assert(r.sidePrev && !r.prevLongPressed);
  // A front key held still skips a chapter, and it is no side turn.
  r = pass(right, -1, -1);
  assert(r.next && r.sideTurn == 0);
  r = pass(-1, -1, right);
  assert(r.nextLongPressed && !r.sidePrev);
  assert(ReaderUtils::holdSkipsChapters());
  // SIDE_HOLD_BACK keeps its release turn and its Back.
  SETTINGS.longPressButtonBehavior = CrossPointSettings::SIDE_HOLD_BACK;
  r = pass(fwd, -1, -1);
  assert(!r.next);
  r = pass(-1, fwd, -1);
  assert(r.next);
  r = pass(-1, -1, fwd);
  assert(r.sideBack && !r.sidePrev && !r.nextLongPressed);
  // Chapter skip: a held side key is still a chapter skip.
  SETTINGS.longPressButtonBehavior = CrossPointSettings::CHAPTER_SKIP;
  r = pass(-1, -1, fwd);
  assert(r.nextLongPressed && !r.sidePrev && !r.sideBack);
}
'''
with tempfile.TemporaryDirectory(prefix='side-hold-input-') as tmp:
    cpp = Path(tmp) / 'input.cpp'
    exe = Path(tmp) / 'input'
    cpp.write_text(fixture + body + test)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('GREEN: a side key turns on its press and its hold is sidePrev; front holds skip a chapter; SIDE_HOLD_BACK unchanged')
