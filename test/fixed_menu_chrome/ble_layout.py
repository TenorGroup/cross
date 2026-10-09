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

source = (root / 'src/activities/settings/BlePageTurnerActivity.cpp').read_text()
header = (root / 'src/activities/settings/BlePageTurnerActivity.h').read_text()
base = (root / 'src/activities/UiListActivity.cpp').read_text()
owner = re.search(r'bool ownsX3Chrome\(\) const override\s*\{[^}]+\}', header)
owner = owner.group(0).replace(' override', '') if owner else 'bool ownsX3Chrome() const { return false; }'
reserve = function(base, 'void UiListActivity::reserveFixedMenuContent(').replace('UiListActivity::', 'BleBoundary::')
refresh = function(source, 'void BlePageTurnerActivity::refreshValues(')
branch = function(refresh, 'else if (code == ROW_STATUS)')
branch = branch[branch.index('{') + 1:-1]
footer = function(source, 'void BlePageTurnerActivity::drawFooter(')
footer = function(footer, '  if (tenorchrome::x3ScreenLayoutEnabled()')
unit = r'''
#include <cassert>
#include <string>
#include <vector>
struct Rect { int y = 16, height = 500; int bottom() const { return y + height; } };
struct UiScreen {
  Rect area;
  Rect body() { return area; }
  void takeTop(int height) { area.y += height; area.height -= height; }
  void takeBottom(int height) { area.height -= height; }
};
namespace tenorchrome {
bool active = true;
bool x3ScreenLayoutEnabled() { return active; }
bool enabled() { return true; }
bool kTouchShell = false;
int contentTop() { return 128; }
int contentTopUnderTabs() { return 128; }
bool wantsFootBack(const char*) { return false; }
int footBackReserve() { return 0; }
struct Layout { Rect bodyRect; };
template<class Renderer> Layout x3ScreenLayout(const Renderer&) { return {}; }
}
constexpr int SMALL_FONT_ID = 0;
namespace shell { bool uglyParts() { return false; } }
struct UiListActivity { void drawFooter() {} };
struct BleBoundary : UiListActivity {
  bool tabBandDrawn = false;
  std::string name = "BlePageTurner";
  struct Renderer { int getScreenHeight() { return 792; } int getLineHeight(int) { return 33; } } renderer;
  std::string statusText_ = "Bluetooth unavailable in this build", bindNotice_;
  struct Info {
    std::vector<std::string> texts;
    void draw(Renderer&, Rect, std::initializer_list<const char*> lines) {
      for (const auto* line : lines) if (*line) texts.emplace_back(line);
    }
  } x3Info;
  OWNER
  void reserveFixedMenuContent(UiScreen& screen);
  void drawX3Footer() { FOOTER }
};
RESERVE
int main() {
  BleBoundary page;
  UiScreen screen;
  page.reserveFixedMenuContent(screen);
  assert(screen.body().y == 16);
  struct Item { const char* label = "Status"; const char* value = nullptr; const char* subtitle = nullptr; } item;
  std::string statusValue_ = "Bluetooth unavailable in this build";
  STATUS
  assert(std::string(item.label) == "Status");
  assert(item.value == nullptr);
  assert(item.subtitle && std::string(item.subtitle) == statusValue_);
  for (bool tips : {false, true}) {
    page.x3Info.texts.clear();
    page.drawX3Footer();
    assert(page.x3Info.texts.empty());
    assert(std::string(item.subtitle) == statusValue_);
  }
  tenorchrome::active = false;
  UiScreen legacy;
  page.reserveFixedMenuContent(legacy);
  assert(legacy.body().y == 128);
}
'''.replace('OWNER', owner).replace('RESERVE', reserve).replace('STATUS', branch).replace('FOOTER', footer)
args.work.mkdir(parents=True, exist_ok=True)
path = args.work / 'ble.cpp'
path.write_text(unit)
program = args.work / 'ble'
subprocess.run(['c++', '-std=c++20', '-UNDEBUG', str(path), '-o', str(program)], check=True)
subprocess.run([str(program)], check=True)
print('GREEN: BLE body starts at 16 and status has its own full-width line')
