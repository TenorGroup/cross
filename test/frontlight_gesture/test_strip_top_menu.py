"""Compile the actual status-strip routing of ActivityManager::loop and the touch branch of drawHeader.

Touch shell: a tap on the strip opens the top menu on every screen that draws it, sub-screens included,
and the strip is no longer a way back.
"""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
manager = (repo / 'src/activities/ActivityManager.cpp').read_text()
start = manager.index('    // Tap-first control-center entry')
end = manager.index('    // Note: do not hold a lock here', start)
route = manager[start:end]
chrome = (repo / 'src/components/TenorMenuChrome.cpp').read_text()
h0 = chrome.index('void tenorchrome::drawHeader(')
h1 = chrome.index('  constexpr int x = 18, rightReserve', h0)
# The touch branch only: the button boards' hand drawn header (tenor/ugly) follows it.
ugly = chrome.find('  if (shell::uglyParts())', h0, h1)
if ugly >= 0:
    h1 = ugly
header = chrome[h0:h1] + '  return;\n}\n'
fixture = r'''
#include <cassert>
#include <string>
namespace BoardConfig { bool hasTouch() { return true; } }
namespace HeaderBackTapTarget {
int sets = 0; bool strip = false; bool foot = false;
void set(int, int, int, int) { ++sets; }
void clear() {}
bool contains(int, int) { return foot; }
}
struct GfxRenderer { int getScreenWidth() const { return 480; } };
namespace tenorchrome {
constexpr bool kTouchShell = true;
constexpr int TOUCH_STRIP_HEIGHT = 32;
int tabTop() { return 32; }
std::string noted;
void noteScreenTitle(const char* t) { noted = t; }
void drawHeader(const GfxRenderer& r, const char* title, const char* prefix, const char* note = nullptr);
}
struct Input {
  bool tapped = false, swipe = false; int x = 0, y = 0;
  bool hasTouch() const { return true; }
  bool wasScreenTapped(int& tx, int& ty) const { tx = x; ty = y; return tapped; }
  bool wasLightPanelGesture() const { return swipe; }
};
struct Activity { std::string name; };
struct Manager {
  Input mappedInput; Activity* currentActivity = nullptr; int menus = 0, loops = 0;
  void openTopMenu() { ++menus; }
  void route();
};
void Manager::route() {
''' + route + r'''  ++loops;
}
namespace tenorchrome {
''' + header.replace('void tenorchrome::drawHeader(', 'void drawHeader(') + r'''
}
int main() {
  Manager m;
  Activity sub{"WifiSelection"}, panel{"FrontlightPanel"}, image{"BmpViewer"};
  // A sub-screen that draws the strip: a tap on it opens the top menu.
  m.currentActivity = &sub; HeaderBackTapTarget::strip = true;
  m.mappedInput.tapped = true; m.mappedInput.y = 10;
  m.route(); assert(m.menus == 1 && m.loops == 0);
  // Below the strip: the screen's own row.
  m.mappedInput.y = 32; m.route(); assert(m.menus == 1 && m.loops == 1);
  // A screen without the strip keeps its taps.
  m.currentActivity = &image; HeaderBackTapTarget::strip = false; m.mappedInput.y = 5;
  m.route(); assert(m.menus == 1 && m.loops == 2);
  // The top-edge swipe still opens it.
  m.mappedInput.tapped = false; m.mappedInput.swipe = true; m.route(); assert(m.menus == 2);
  // The panel itself gets its own input.
  m.currentActivity = &panel; HeaderBackTapTarget::strip = true; m.mappedInput.tapped = true;
  m.route(); assert(m.menus == 2 && m.loops == 3);
  // A screen that names where it came from records no back target on the strip.
  GfxRenderer r;
  tenorchrome::drawHeader(r, "Wi-Fi", "Settings");
  assert(HeaderBackTapTarget::sets == 0 && tenorchrome::noted == "Wi-Fi");
}
'''
with tempfile.TemporaryDirectory(prefix='strip-top-menu-') as tmp:
    cpp = Path(tmp) / 'strip.cpp'
    exe = Path(tmp) / 'strip'
    cpp.write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('GREEN: the strip opens the top menu on every screen that draws it and is no way back')
