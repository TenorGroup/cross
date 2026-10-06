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
  fui::Rect skinChoices_[1]{};
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

    def test_preview_layout_gate_holds_for_the_tick_and_the_loop(self):
        text = (REPO / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        names = ['catchUpTick', 'skipLoopDelay', 'dropCatchUp']
        if re.search(r'\bEpubReaderActivity::catchUpCanTick\s*\(', text):
            names.append('catchUpCanTick')
        methods = '\n'.join(function(text, 'EpubReaderActivity', name) for name in names)
        result = run(r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#define LOG_INF(...) ((void)0)
unsigned long now = 10000;
unsigned long millis() { return now; }
size_t freeHeap = 90000, maxBlock = 40000;
struct { size_t getFreeHeap() { return freeHeap; } size_t getMaxAllocHeap() { return maxBlock; } } ESP;
bool bleDefers = false, bleStarting = false;
struct BleStatus { bool starting; };
namespace bleturner { BleStatus status() { return {bleStarting}; } }
struct RenderLock { struct TryTake {}; explicit RenderLock(TryTake) {} bool acquired() const { return true; } };
struct HalPowerManager { struct Lock { Lock() {} ~Lock() {} }; };
struct ReaderRenderSpec {};
struct { ReaderRenderSpec readerRenderSpec(int, int) { return {}; } } SETTINGS;
struct Epub {};
struct GfxRenderer {} renderer;
constexpr bool preview = true;
int steps = 0;
struct Section {
  int currentPage = 0, pageCount = 0;
  bool building = true, starved = false, reached = false;
  Section(const std::shared_ptr<Epub>&, int, GfxRenderer&, bool) {}
  bool loadSectionFile(const ReaderRenderSpec&) { return false; }
  bool isPartial() const { return false; }
  bool coversVisibleTextOffset(uint32_t) const { return false; }
  bool startBuild(const ReaderRenderSpec&) { return true; }
  bool isBuilding() const { return building; }
  bool buildSomeMore(int) { ++steps; return !starved; }
  bool buildStarved() const { return starved; }
  bool buildReachedVisibleTextOffset(uint32_t) const { return reached; }
  std::optional<int> getPageForVisibleTextOffset(uint32_t) const { return 3; }
  bool parkBuild() { building = false; return true; }
  void abandonBuild() {}
};
struct EpubReaderActivity {
  std::unique_ptr<Section> section, catchUp;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  int currentSpineIndex = 0, nextPageNumber = 0;
  uint16_t buildViewportWidth = 480, buildViewportHeight = 800;
  bool xemTruoc = true, xemTruocTrenMan = false;
  int8_t xemTruocLat = 0;
  uint32_t xemTruocDich = 4386;
  unsigned long xemTruocInputMs = 0;
  static constexpr unsigned long CATCH_UP_QUIET_MS = 400;
  static constexpr uint8_t CATCH_UP_MAX_FAILS = 3;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 50000, BACKGROUND_BUILD_MIN_MAX_ALLOC = 20000;
  uint8_t catchUpFails = 0;
  bool deferBackgroundBuildForBle() const { return bleDefers; }
  bool backgroundBuildCanTick() { return false; }
  bool backgroundBuildWanted() const { return false; }
  bool catchUpCanTick() const;
  void catchUpTick(bool inputThisPass);
  bool skipLoopDelay();
  void dropCatchUp();
};
''' + methods + r'''
int main() {
  EpubReaderActivity reader;
  reader.catchUpTick(false);
  assert(reader.catchUp && reader.catchUp->isBuilding());
  assert(reader.skipLoopDelay() && "a running preview layout keeps the loop awake");
  reader.catchUpFails = EpubReaderActivity::CATCH_UP_MAX_FAILS;
  assert(!reader.skipLoopDelay() && "a layout given up on spins the loop at full speed");
  reader.catchUpFails = 0;
  reader.xemTruocInputMs = now;
  assert(!reader.skipLoopDelay() && "the loop spins through the quiet time after a press");
  reader.xemTruocInputMs = 0;
  bleDefers = true;
  assert(!reader.skipLoopDelay() && "the loop spins while the radio holds the heap");
  bleDefers = false;
  assert(reader.skipLoopDelay());
  const int stepsBefore = steps;
  maxBlock = 8000;
  reader.catchUpTick(false);
  assert(steps == stepsBefore && "a preview layout step ran below the background build heap floor");
  assert(reader.catchUpFails == 0 && reader.catchUp);
  maxBlock = 40000;
  reader.xemTruocInputMs = 0;
  reader.catchUpTick(false);
  assert(steps == stepsBefore + 1);
  reader.catchUp->reached = true;
  reader.xemTruocLat = 1;
  reader.catchUpTick(false);
  assert(!reader.section && "the layout landed under a turn asked from the preview and dropped it");
  reader.xemTruocLat = 0;
  reader.catchUpTick(false);
  assert(reader.section && !reader.xemTruoc && reader.xemTruocTrenMan);
}
''')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_turning_the_screen_drops_the_preview_layout_of_the_old_viewport(self):
        text = (REPO / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        methods = '\n'.join(function(text, 'EpubReaderActivity', name)
                            for name in ('applyOrientation', 'dropCatchUp'))
        result = run(r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
int abandoned = 0;
struct { uint8_t orientation = 0; bool saveToFile() { return true; } } SETTINGS;
struct GfxRenderer {} renderer;
namespace ReaderUtils { void applyOrientation(GfxRenderer&, uint8_t) {} }
struct Section {
  int currentPage = 0, pageCount = 0;
  void abandonBuild() { ++abandoned; }
};
struct EpubReaderActivity;
struct RenderLock { explicit RenderLock(EpubReaderActivity&) {} ~RenderLock() {} };
struct EpubReaderActivity {
  std::unique_ptr<Section> section, catchUp;
  int cachedSpineIndex = 0, currentSpineIndex = 0, cachedChapterTotalPageCount = 0, nextPageNumber = 0;
  uint8_t appliedOrientation = 0;
  void rememberCurrentContentOffset() {}
  void applyOrientation(uint8_t orientation);
  void dropCatchUp();
};
''' + methods + r'''
int main() {
  EpubReaderActivity reader;
  reader.catchUp = std::make_unique<Section>();
  reader.applyOrientation(1);
  assert(!reader.catchUp && abandoned == 1 && "the preview layout of the old viewport would land");
  assert(reader.appliedOrientation == 1 && SETTINGS.orientation == 1);
}
''')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_auto_turn_relayout_drops_the_preview_layout(self):
        text = (REPO / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        methods = '\n'.join(function(text, 'EpubReaderActivity', name)
                            for name in ('toggleAutoPageTurn', 'dropCatchUp'))
        result = run(r'''
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>
int abandoned = 0;
unsigned long millis() { return 1234; }
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
struct UITheme {
  static UITheme& getInstance() { static UITheme t; return t; }
  uint8_t getProgressBarHeight() const { return 4; }
};
struct RenderLock { RenderLock() {} ~RenderLock() {} };
struct Section {
  int currentPage = 0, pageCount = 0;
  void abandonBuild() { ++abandoned; }
};
struct EpubReaderActivity {
  std::unique_ptr<Section> section, catchUp;
  int cachedSpineIndex = 0, currentSpineIndex = 0, cachedChapterTotalPageCount = 0, nextPageNumber = 0;
  bool automaticPageTurnActive = false;
  unsigned long lastPageTurnTime = 0, pageTurnDuration = 0;
  uint8_t statusBar = 0;
  uint8_t readerStatusBarHeight() const { return statusBar; }
  void rememberCurrentContentOffset() {}
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void dropCatchUp();
};
''' + methods + r'''
int main() {
  EpubReaderActivity reader;
  reader.catchUp = std::make_unique<Section>();
  reader.toggleAutoPageTurn(2);
  assert(reader.automaticPageTurnActive);
  assert(!reader.catchUp && abandoned == 1 && "the preview layout of the old page height would land");
}
''')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_toolbar_actions_route_once_with_24_targets(self):
        text = (REPO / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
        header = (REPO / 'src/activities/reader/ReaderToolbarUi.h').read_text()
        host = (REPO / 'src/components/UiAppHost.h').read_text()
        alias = re.search(r'using UiApp = .*?;', host).group(0)
        events = re.search(r'  enum class Event \{.*?;', header, re.S).group(0)
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
