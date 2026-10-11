from pathlib import Path
import argparse
import re
import subprocess
import sys
from shared_path_fixture import bitmap_build_args

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--mutate', action='store_true')
args = parser.parse_args()
args.work.mkdir(parents=True, exist_ok=True)


def method(source, name):
    start = source.rfind('\n', 0, source.index(name)) + 1
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


source = (root / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
assert 'ReaderToolbarUi::screenLayout(' in source, 'RED: toolbar lacks the shared bar'
assert 'buildX3Bar(screen)' in method(source, 'ReaderToolbarUi::screenFn(')
bar = method(source, 'ReaderToolbarUi::buildX3Bar(')
assert bar.count('readermenu::x3TabIcon(') == 1 and 'icon_tenor_' not in bar, 'RED: toolbar must use shared reader icons'
assert 'screenLayout().bodyRect' in method(source, 'ReaderToolbarUi::buildSheet(')
for name in ('buildToolbar', 'buildPanel'):
    assert 'usesX3Bar() ? 0 : kToolRowH' in method(source, f'ReaderToolbarUi::{name}(')
subprocess.run([sys.executable, str(Path(__file__).with_name('run.py')),
                '--work', str(args.work / 'home')], check=True)
fixture = (args.work / 'home/layout.cpp').read_text()
fixture = fixture[:fixture.rfind('int main(')]
fixture = fixture.replace('bool isUgly() { return false; }',
                          'bool handwritten = false; bool isUgly() { return handwritten; }')
fixture += '''
namespace tenorchrome {
'''
chrome = (root / 'src/components/TenorMenuChrome.h').read_text()
for name in ('BAR_TAB_W', 'BAR_END_INSET', 'FOOT_BACK_X', 'X3_BAR_MARGIN'):
    fixture += re.search(rf'^constexpr int {name} = .*?;', chrome, re.M).group() + '\n'
fixture += re.search(r'^constexpr int barTabCentre\(.*?^}\n', chrome, re.M | re.S).group()
fixture += '''
}
namespace freeink::ui { using Rect = tenorchrome::X3ScreenRect; }
namespace readermenu {
enum class Tab : uint8_t { FAVORITES, POSITION, READING, TOOLS, TEXT };
enum class Tool : uint8_t { FAVORITES, CONTENTS, TEXT, MORE };
constexpr int X3_TAB_COUNT = 5;
}
class ReaderToolbarUi {
 public:
  static constexpr int kToolCount = 4;
  static bool usesX3Bar(); static int toolCount(); static int textTool();
  struct Model { bool panel = false; int activeTool = 0; const char* tipText = nullptr; } model_;
  GfxRenderer* renderer_;
  tenorchrome::X3ScreenLayout screenLayout() const;
  fui::Rect toolRect(int index) const;
};
'''
for name in ('usesX3Bar', 'toolCount', 'textTool', 'screenLayout', 'toolRect'):
    body = method(source, f'ReaderToolbarUi::{name}(')
    if args.mutate and name == 'screenLayout':
        body = body.replace('return tenorchrome::x3ScreenLayout(', 'auto layout = tenorchrome::x3ScreenLayout(')
        body = body[:-1] + ' ++layout.barRect.y; return layout; }'
    fixture += body + '\n'
reader = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
opening = method(reader, 'EpubReaderActivity::openOverlay(')
assert 'panelIndex = ReaderToolbarUi::usesX3Bar() ? 0 :' in opening
assert 'target == Overlay::Contents && !ReaderToolbarUi::usesX3Bar() && waitsForIndex()' in opening
assert 'TEXT_SETTINGS' not in opening
assert 'else if (actionPanel())' in method(reader, 'EpubReaderActivity::handleOverlayInput(')
fixture += '''
class EpubReaderActivity {
 public:
  enum class Overlay { None, Toolbar, Contents, Text, More, Favorites, Reading };
  struct Item { readermenu::Tab tab; };
  std::vector<Item> moreItems;
  Overlay overlay = Overlay::None;
  int panelActionRow(int row) const;
  bool actionPanel() const;
};
'''
fixture += method(reader, 'EpubReaderActivity::panelActionRow(')
fixture += method(reader, 'EpubReaderActivity::actionPanel(')
fixture += '''
int main() {
  GfxRenderer renderer; renderer.screenHeight = 792;
  ReaderToolbarUi toolbar; toolbar.renderer_ = &renderer;
  int cases = 0;
  for (int tier = 0; tier < 3; ++tier) {
    SETTINGS.uiTextSize = tier;
    for (int mode = 0; mode < 3; ++mode) {
      SETTINGS.hidden = mode == 2; SETTINGS.large = mode == 1;
      for (bool tips : {false, true}) {
        SETTINGS.tipsEnabled = tips;
        for (bool panel : {false, true}) for (int tab = 0; tab < 5; ++tab) {
          toolbar.model_ = {panel, tab, tab == 0 ? "Giu OK: Ghim" : nullptr};
          HomeActivity home(renderer);
          const auto expected = home.homeScreenLayout().barRect;
          const auto layout = toolbar.screenLayout();
          assert(layout.barRect.x == expected.x && layout.barRect.y == expected.y &&
                 layout.barRect.width == expected.width && layout.barRect.height == expected.height);
          assert(layout.bodyRect.bottom() + 8 <= layout.barRect.y);
          assert(layout.tipRect.bottom() <= layout.barRect.y);
          for (int index = 0; index < 5; ++index) {
            const auto rect = toolbar.toolRect(index);
            assert(rect.y == expected.y && rect.height == expected.height);
            assert(rect.x + rect.width / 2 == 66 + (2 * (renderer.getScreenWidth() - 133) * index + 4) / 8);
          }
          ++cases;
        }
      }
    }
  }
  assert(ReaderToolbarUi::toolCount() == 5 && ReaderToolbarUi::textTool() == 4);
  EpubReaderActivity reader;
  using Tab = readermenu::Tab;
  reader.moreItems = {{Tab::POSITION}, {Tab::READING}, {Tab::READING}, {Tab::TOOLS}, {Tab::TEXT}};
  reader.overlay = EpubReaderActivity::Overlay::Contents;
  assert(reader.actionPanel());
  assert(reader.panelActionRow(-1) == 1 && reader.panelActionRow(0) == 0 && reader.panelActionRow(1) == -1);
  reader.overlay = EpubReaderActivity::Overlay::Reading;
  assert(reader.actionPanel());
  assert(reader.panelActionRow(-1) == 2 && reader.panelActionRow(0) == 1 && reader.panelActionRow(1) == 2);
  reader.overlay = EpubReaderActivity::Overlay::More;
  assert(reader.actionPanel());
  assert(reader.panelActionRow(-1) == 1 && reader.panelActionRow(0) == 3);
  shell::handwritten = true;
  assert(!ReaderToolbarUi::usesX3Bar() && ReaderToolbarUi::toolCount() == 4 && ReaderToolbarUi::textTool() == 2);
  assert(reader.panelActionRow(-1) == 5 && reader.panelActionRow(4) == 4);
  reader.overlay = EpubReaderActivity::Overlay::Contents;
  assert(!reader.actionPanel());
  reader.overlay = EpubReaderActivity::Overlay::Reading;
  assert(!reader.actionPanel());
  printf("GREEN toolbar: %d layer/tab cases, exact Home bar and centers, shared action IDs, ugly unchanged\\n", cases);
}
'''
unit = args.work / 'toolbar.cpp'
unit.write_text(fixture)
subprocess.run(['c++', '-std=c++17', '-O0', '-UNDEBUG', *bitmap_build_args(root, args.work), '-I', str(root / 'src'),
                '-I', str(root / 'lib/EpdFont'), '-I', str(root / 'lib/Utf8'),
                '-I', str(root / 'lib/GfxRenderer'), '-I', str(root / 'freeink-sdk/libs/assets/Icons/include'),
                str(unit), str(root / 'lib/Utf8/Utf8.cpp'), str(root / 'lib/EpdFont/EpdFont.cpp'),
                str(root / 'lib/EpdFont/EpdFontFamily.cpp'), '-o', str(args.work / 'toolbar')], check=True)
subprocess.run([str(args.work / 'toolbar')], check=True)
