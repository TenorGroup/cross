"""Where a list starts on the X3 and X4 Classic: the shared reservation every list screen runs (UiListActivity).

The production reserveFixedMenuContent, x3ScreenLayout and statusOnTop are compiled against the real header
constants. A list that neither draws its own X3 chrome nor a tab band (the Connections hub, the settings choice and
Home key screens) used to be pushed under the old top tab band (y 129 at the small text size) although that band
is empty since the X3 bar moved to the foot. It must start at the layout's body, under the status strip or at the
top of the glass, at 3 text sizes x status bar top/bottom x opened from Home or from a book, on both devices.
"""
from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]


def function(source, name):
    start = source.rfind('\n', 0, source.index(name)) + 1
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


header = (root / 'src/components/TenorMenuChrome.h').read_text()
chrome = (root / 'src/components/TenorMenuChrome.cpp').read_text()
lists = (root / 'src/activities/UiListActivity.cpp').read_text()

constants = '\n'.join(re.search(r'constexpr int ' + name + r' = [^;]+;', header).group(0)
                      for name in ('HEADER_TOP', 'HEADER_HEIGHT', 'TAB_HEIGHT', 'TOUCH_STATUS_TOP_INSET',
                                   'TOUCH_STRIP_HEIGHT'))
inlines = '\n'.join(function(header, 'inline int ' + name + '()')
                    for name in ('headerHeight', 'tabTop', 'tabHeight', 'contentTop', 'contentTopUnderTabs')
                    if 'inline int ' + name + '()' in header)
structs = function(header, 'struct X3ScreenRect') + ';\n' + function(header, 'struct X3ScreenLayout') + ';'
definitions = '\n'.join(function(chrome, 'tenorchrome::' + name + '(')
                        for name in ('x3ScreenLayoutEnabled', 'statusOnTop', 'x3ScreenLayout'))
reserve = function(lists, 'void UiListActivity::reserveFixedMenuContent(')

unit = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include "components/UIScale.h"
namespace EpdFontFamily { enum Style { REGULAR }; }
struct Settings {
  uint8_t uiTextSize = 0;
  bool statusTop = false, tenorButtonSymbols = true, tipsEnabled = true;
  struct Spec { bool visible, top, large; };
  Spec globalStatusBarSpec() const { return {true, statusTop, false}; }
} SETTINGS;
struct GfxRenderer {
  int width = 528, height = 792;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  int getTextInkTop(int, const char*, EpdFontFamily::Style) const { return 3; }
  int getLineHeight(int) const { return uiTextSizeSpec(SETTINGS.uiTextSize).captionLineHeight; }
};
struct UITheme {
  struct Metrics { int buttonHintsHeight = 40; };
  static UITheme& getInstance() { static UITheme theme; return theme; }
  Metrics getMetrics() const { return {}; }
};
struct { bool reader = false; bool isReaderActivity() const { return reader; } } activityManager;
namespace shell { bool ugly = false; bool uglyParts() { return ugly; } }
namespace tenorchrome {
bool kTouchShell = false;
bool chrome = true;
bool enabled() { return chrome; }
@@CONSTANTS@@
@@INLINES@@
// The foot status band: the same number for the screen's own margin and the reservation, so any value serves.
int statusIconTopY(int height, bool) { return height - 30; }
int statusTextY(int height, bool) { return height - 28; }
int smallFooterSymbolsTopY(const GfxRenderer& renderer) { return renderer.getScreenHeight() - 31; }
bool wantsFootBack(const char*) { return false; }
int footBackReserve() { return 0; }
@@STRUCTS@@
bool x3ScreenLayoutEnabled();
bool statusOnTop();
X3ScreenLayout x3ScreenLayout(const GfxRenderer& renderer, int requestedTipHeight = 0, int informationHeight = 0);
}
@@DEFINITIONS@@
struct UiScreen {
  struct Rect { int y, height; int bottom() const { return y + height; } } area{0, 0};
  Rect body() const { return area; }
  void takeTop(int16_t height) { area.y += height; area.height -= height; }
  void takeBottom(int16_t height) { area.height -= height; }
};
struct UiListActivity {
  bool tabBandDrawn = false, owns = false;
  std::string name = "NetworkModeSelection";
  GfxRenderer renderer;
  bool ownsX3Chrome() const { return owns; }
  void reserveFixedMenuContent(UiScreen& screen);
};
@@RESERVE@@

// A screen whose margin runs from `top` to `bottom`, after the shared reservation.
UiScreen::Rect reserved(UiListActivity& page, int top, int bottom) {
  UiScreen screen;
  screen.area = {top, bottom - top};
  page.reserveFixedMenuContent(screen);
  return screen.body();
}

int main() {
  struct Device { const char* name; int width, height; };
  int cases = 0;
  for (const Device device : {Device{"X3", 528, 792}, Device{"X4C", 480, 800}}) {
    for (uint8_t size = 0; size < 3; ++size) {
      for (const bool statusTop : {false, true}) {
        for (const bool fromBook : {false, true}) {
          SETTINGS.uiTextSize = size;
          SETTINGS.statusTop = statusTop;
          activityManager.reader = fromBook;
          tenorchrome::kTouchShell = false;
          shell::ugly = false;
          UiListActivity page;
          page.renderer.width = device.width;
          page.renderer.height = device.height;
          const auto body = tenorchrome::x3ScreenLayout(page.renderer).bodyRect;
          // The status strip is drawn at the top only away from a book; the body follows it either way.
          assert(body.y == (statusTop && !fromBook ? tenorchrome::TOUCH_STRIP_HEIGHT + 16 : 16));
          // The hub and the settings sub-screens lay their margin on the body: it stays.
          const auto hub = reserved(page, body.y, body.bottom());
          if (hub.y != body.y || hub.bottom() != body.bottom()) {
            std::printf("RED: %s size %d status %s from %s: list starts at %d, body at %d\n", device.name, size,
                        statusTop ? "top" : "bottom", fromBook ? "book" : "Home", hub.y, body.y);
            return 1;
          }
          // A screen that does not know the X3 layout keeps to the body too.
          const auto plain = reserved(page, 0, device.height);
          assert(plain.y == body.y && plain.bottom() == body.bottom());
          // Content the screen put above its list (a chart, an information line) keeps its place.
          const auto below = reserved(page, body.y + 200, body.bottom());
          assert(below.y == body.y + 200 && below.bottom() == body.bottom());
          // A tab band or the screen's own X3 chrome: the reservation leaves the margin alone.
          page.tabBandDrawn = true;
          assert(reserved(page, 3, device.height).y == 3);
          page.tabBandDrawn = false;
          page.owns = true;
          assert(reserved(page, 3, device.height).y == 3);
          page.owns = false;
          // tenor/ugly: inside the body as before.
          shell::ugly = true;
          const auto ugly = tenorchrome::x3ScreenLayout(page.renderer).bodyRect;
          const auto uglyBody = reserved(page, 0, device.height);
          assert(uglyBody.y == ugly.y && uglyBody.bottom() == ugly.bottom());
          std::printf("GREEN: %s size %d status %s from %s: list starts at %d\n", device.name, size,
                      statusTop ? "top" : "bottom", fromBook ? "book" : "Home", hub.y);
          ++cases;
        }
      }
    }
  }
  // The touch shell keeps its own top, under the status strip.
  tenorchrome::kTouchShell = true;
  shell::ugly = false;
  UiListActivity touch;
  touch.renderer.width = 480;
  touch.renderer.height = 800;
  assert(reserved(touch, 0, 800).y == tenorchrome::contentTop());
  std::printf("GREEN: %d X3/X4C cases, touch keeps its strip top\n", cases);
  return 0;
}
'''
unit = (unit.replace('@@CONSTANTS@@', constants).replace('@@INLINES@@', inlines).replace('@@STRUCTS@@', structs)
        .replace('@@DEFINITIONS@@', definitions).replace('@@RESERVE@@', reserve))
args.work.mkdir(parents=True, exist_ok=True)
path = args.work / 'list_body.cpp'
path.write_text(unit)
program = args.work / 'list_body'
subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I' + str(root / 'src'), str(path), '-o', str(program)], check=True)
raise SystemExit(subprocess.run([str(program)]).returncode)
