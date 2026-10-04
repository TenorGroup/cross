"""Run the production queue and toolbar routing against deterministic host boundaries."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def function(text, owner, name):
    match = re.search(r'\b' + owner + '::' + name + r'\s*\(', text)
    start = text.rfind('\n', 0, match.start()) + 1
    brace = text.index('{', match.end())
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def run(source, sdk=False):
    with tempfile.TemporaryDirectory(prefix='reader-menu-runtime-') as tmp:
        cpp, program = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
        cpp.write_text(source)
        command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                   '-fsanitize=address,undefined', str(cpp), '-o', str(program)]
        if sdk:
            root = REPO / 'freeink-sdk/libs/ui/FreeInkUI'
            command += ['-I' + str(root / 'include'), str(root / 'src/FreeInkUI.cpp')]
        compiled = subprocess.run(command, capture_output=True, text=True)
        assert compiled.returncode == 0, compiled.stdout + compiled.stderr
        return subprocess.run([str(program)], capture_output=True, text=True)


class ReaderMenuRuntimeTest(unittest.TestCase):
    def test_dense_choice_rows_reserve_label_space(self):
        text = (REPO / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
        method = function(text, 'ReaderToolbarUi', 'drawChoices')
        result = run(r'''
#include <FreeInkApp.h>
#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
namespace fui = freeink::ui;
namespace freeink { struct Icon {}; }
namespace freeink::ui { BitmapRef bitmapFromIcon(const freeink::Icon&) { return {}; } }
struct Target : fui::DrawTarget {
  int width = 250;
  std::vector<fui::Rect> labels, pills;
  fui::Size measureText(fui::FontId,const char*,fui::TextStyle) const override {
    return {static_cast<int16_t>(width), 24};
  }
  int16_t lineHeight(fui::FontId) const override { return 24; }
  void fill(fui::Rect,fui::Paint,uint8_t=0,uint8_t=fui::CornersAll) override {}
  void stroke(fui::Rect r,fui::Paint,uint8_t,uint8_t=0,uint8_t=fui::CornersAll) override { pills.push_back(r); }
  void line(fui::Point,fui::Point,uint8_t,fui::Paint) override {}
  void triangle(fui::Point,fui::Point,fui::Point,fui::Paint) override {}
  void text(fui::Rect r,const char*,fui::TextStyle) override { labels.push_back(r); }
  void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint=fui::Paint::solid(fui::Color::Black),fui::Rotation=fui::Rotation::None) override {}
};
struct UiScreen {
  Target target_;
  fui::ThemeTokens tokens;
  struct Frame { void hit(fui::Rect,fui::ActionId,int16_t,uint16_t) {} } frame_;
  UiScreen() { tokens.spaceSm=8; }
  Target& target() { return target_; }
  const fui::ThemeTokens& theme() const { return tokens; }
  Frame& frame() { return frame_; }
};
constexpr fui::ActionId ACTION_CHOICE=7;
struct ReaderToolbarUi {
  static constexpr int kChoiceStride=8;
  struct Model {
    bool denseRows=true;
    std::function<int(int)> choiceCount=[](int){return 5;};
    std::function<int(int)> choiceInUse=[](int){return 0;};
    std::function<const freeink::Icon*(int,int)> choiceIcon=[](int,int){return nullptr;};
  } model_;
  struct Nav { int top=0; } nav_;
  fui::ListProps listProps_;
  std::string windowLabels_[1]={"Paragraph Alignment"};
  ReaderToolbarUi() { listProps_.rowInset=20; listProps_.sidePadding=8; }
  void drawChoices(UiScreen&,const fui::Rect&,int16_t,int16_t,int);
};
''' + method + r'''
int main() {
  ReaderToolbarUi ui;
  for (int labelWidth : {250,170,310,400}) {
    UiScreen screen;
    screen.target_.width=labelWidth;
    ui.drawChoices(screen,{0,0,528,56},56,0,1);
    assert(screen.target_.pills.size()==1);
    const int firstChoiceLeft=screen.target_.pills.front().x-3;
    const int labelLeft=28;
    const int labelRight=screen.target_.labels.empty()
        ? labelLeft+labelWidth : screen.target_.labels.front().right();
    assert(labelRight+screen.tokens.spaceSm<=firstChoiceLeft && "label touches the first choice");
    assert(screen.target_.labels.size()==1);
  }
  UiScreen touch;
  ui.model_.denseRows=false;
  ui.drawChoices(touch,{0,0,528,60},60,0,1);
  assert(touch.target_.pills.front().x==263); // unchanged 48px touch cells
}
''', sdk=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_replaces_a_running_layout_and_keeps_the_preview_checkpoint(self):
        text = (REPO / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        methods = '\n'.join(function(text, 'EpubReaderActivity', name)
                            for name in ('danLaiTrang', 'dropCatchUp'))
        result = run(r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
int abandoned = 0, destroyed = 0;
uint32_t millis() { return 1234; }
struct Section {
  int currentPage = 12, pageCount = 40;
  void abandonBuild() { ++abandoned; }
  ~Section() { ++destroyed; }
};
struct EpubReaderActivity {
  std::unique_ptr<Section> section, catchUp;
  std::optional<uint32_t> cachedVisibleTextOffset;
  int cachedSpineIndex = 0, currentSpineIndex = 0, cachedChapterTotalPageCount = 0, nextPageNumber = 0;
  bool xemTruocTrenMan = false, xemTruoc = false;
  int xemTruocLat = 0, catchUpFails = 0;
  uint32_t xemTruocDich = 0, xemTruocInputMs = 0;
  void rememberCurrentContentOffset() { cachedVisibleTextOffset = 4000; }
  void danLaiTrang();
  void dropCatchUp();
};
''' + methods + r'''
int main() {
  EpubReaderActivity reader;
  reader.cachedVisibleTextOffset = 4386;
  reader.xemTruocDich = 4386;
  reader.xemTruoc = true;
  reader.catchUp = std::make_unique<Section>();
  reader.danLaiTrang();
  assert(!reader.catchUp && "the previous request is still running");
  assert(abandoned == 1 && destroyed == 1);
  assert(reader.xemTruoc && reader.xemTruocDich == 4386);
  reader.catchUp = std::make_unique<Section>();
  reader.danLaiTrang();
  assert(!reader.catchUp && abandoned == 2 && destroyed == 2);
  assert(reader.xemTruocDich == 4386 && reader.xemTruocInputMs == 1234);
  reader.section = std::make_unique<Section>();
  reader.xemTruocTrenMan = true;
  reader.danLaiTrang();
  assert(!reader.section && abandoned == 3 && destroyed == 3);
  assert(reader.xemTruocDich == 4386 && reader.nextPageNumber == 12);
}
''')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_toolbar_actions_route_once_with_24_targets(self):
        text = (REPO / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
        header = (REPO / 'src/activities/reader/ReaderToolbarUi.h').read_text()
        host = (REPO / 'src/components/UiAppHost.h').read_text()
        alias = re.search(r'using UiApp = .*?;', host).group(0)
        events = re.search(r'  enum class Event \{.*?;', header).group(0)
        routed = re.search(r'  struct Routed \{.*?\n  };', header, re.S).group(0)
        constants = '\n'.join(re.findall(r'^constexpr fui::ActionId .*?;', text, re.M)).replace(
            'constexpr', '[[maybe_unused]] constexpr')
        methods = '\n'.join(function(text, 'ReaderToolbarUi', name) for name in ('begin', 'route', 'onAction'))
        result = run(r'''
#include <FreeInkApp.h>
#include <cassert>
#include <cstdio>
namespace fui = freeink::ui;
struct Target : fui::DrawTarget {
  fui::Size measureText(fui::FontId,const char*,fui::TextStyle) const override { return {8,16}; }
  int16_t lineHeight(fui::FontId) const override { return 16; }
  void fill(fui::Rect,fui::Paint,uint8_t=0,uint8_t=fui::CornersAll) override {}
  void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t=0,uint8_t=fui::CornersAll) override {}
  void line(fui::Point,fui::Point,uint8_t,fui::Paint) override {}
  void triangle(fui::Point,fui::Point,fui::Point,fui::Paint) override {}
  void text(fui::Rect,const char*,fui::TextStyle) override {}
  void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint=fui::Paint::solid(fui::Color::Black),fui::Rotation=fui::Rotation::None) override {}
};
struct MappedInputManager { fui::InputSnapshot snap; };
''' + constants + '\nstruct ReaderToolbarUi {\n' + alias + '\n' + events + '\n' + routed + r'''
  Target target;
  static fui::DeviceContext device() {
    fui::DeviceContext value;
    value.width = 240; value.height = 40; value.hasTouch = true; value.minTouchSize = 10;
    return value;
  }
  UiApp app{target, device()};
  using UiScreen = UiApp::ScreenType;
  Routed pending_;
  struct Nav { void reset() {} } nav_;
  void resetUi() {}
  struct Touch { fui::ActionEvent event; fui::InputSnapshot snap; bool routed; };
  Touch routeTouch(const MappedInputManager& input, bool, bool) {
    return {app.route(input.snap), input.snap, true};
  }
  static void screenFn(UiScreen& screen, void*) {
    for (int i=0; i<24; ++i)
      screen.frame().hit({static_cast<int16_t>(i*10),0,10,40},
                         static_cast<fui::ActionId>(i%7+1),static_cast<int16_t>(i),
                         i%7+1 == ACTION_SCRUB ? fui::InputTouch | fui::InputDrag : fui::InputTouch);
  }
  static void onAction(const fui::ActionEvent&,void*);
  void begin();
  Routed route(const MappedInputManager&);
};
''' + methods + r'''
int main() {
  ReaderToolbarUi ui;
  ui.begin();
  ui.app.render();
  for (int i=0; i<24; ++i) {
    MappedInputManager input;
    input.snap.touchX = i*10+5;
    input.snap.touchY = 20;
    input.snap.touchPressed = true;
    ui.route(input);
    input.snap.touchPressed = false;
    input.snap.touchReleased = true;
    auto routed = ui.route(input);
    if (static_cast<int>(routed.event) != i%7+1)
      fprintf(stderr, "target=%d event=%d value=%d sdk=%d\n", i, static_cast<int>(routed.event), routed.value, ui.app.lastEvent().action);
    assert(static_cast<int>(routed.event) == i%7+1 && "toolbar action was lost");
    assert(routed.value == i && routed.routed);
  }
  assert(!ui.app.interactionOverflowed());
  assert(!ui.app.handlerOverflowed());
}
''', sdk=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
