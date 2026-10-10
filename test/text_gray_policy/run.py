import argparse
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--mutate', action='store_true')
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
reader = args.repo / 'src/activities/reader'
epub = (reader / 'EpubReaderActivity.cpp').read_text()
txt = (reader / 'TxtReaderActivity.cpp').read_text()
utils = (reader / 'ReaderUtils.h').read_text()


def block(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


policy = reader / 'TextGrayPolicy.h'
if args.mutate:
    content = policy.read_text()
    original = block(content, 'inline bool shouldRunTextGrayscale(')
    mutant = original[:original.index('{') + 1] + '\n  return context.textAntiAliasing;\n}'
    (args.out / 'TextGrayPolicy.h').write_text(content.replace(original, mutant))
policy_include = '#include "TextGrayPolicy.h"' if policy.exists() else r'''
struct TextGrayContext {
  bool textAntiAliasing = false, covered = false, preparingPopup = false;
  bool pendingManualTurn = false, pendingExternalTurn = false;
  bool pageBeingLeft = false, nextScreenWaiting = false;
  bool pendingScreenshot = false, backgroundWork = false, preview = false;
};
bool shouldRunTextGrayscale(const TextGrayContext& context) { return context.textAntiAliasing; }
'''
context_method = block(epub, 'TextGrayContext EpubReaderActivity::textGrayContext() const') if policy.exists() else r'''
TextGrayContext EpubReaderActivity::textGrayContext() const {
  TextGrayContext context;
  context.textAntiAliasing = SETTINGS.textAntiAliasing;
  context.covered = overlay != Overlay::None || overlayPopup.isActive();
  context.preparingPopup = showBookmarkMessage || showDictionaryMessage || showIndexingMessage ||
                           buildPopupPending || buildPopupRefreshing || pendingSyncSaveError;
  context.pendingManualTurn = pendingManualTurn != 0;
  context.pendingExternalTurn = pendingExternalTurn != 0;
  context.pageBeingLeft = pageBeingLeft() != nullptr;
  context.nextScreenWaiting = nextScreenWaiting() != nullptr;
  context.pendingScreenshot = pendingScreenshot;
  context.preview = preview;
  return context;
}
'''
gray_body = epub[epub.index('  const bool pageHasImages = page->hasImages();'):epub.index('  if (grayscaleCompleted) markClosedTextFrameUpLocked();')]
helper = block(utils, 'inline void displayWithRefreshCycle(') + '\n' + block(utils, 'inline void displayBaseWithRefreshCycle(')
helper += '\n' + block(utils, 'template <typename RenderFn')
txt_body = block(txt, 'void TxtReaderActivity::renderPage(')
txt_tail_start = '  const auto grayContext' if policy.exists() else '  if (SETTINGS.textAntiAliasing)'
txt_tail = txt_body[txt_body.index(txt_tail_start):]

harness = r'''
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
POLICY_INCLUDE
int checks = 0;
struct Counts {
  int allocations = 0, snapshot = 0, base = 0, gray = 0, cleanup = 0;
  int text = 0, bwText = 0, images = 0, bw = 0, failures = 0;
} counts;
void check(bool condition, const char* name) {
  ++checks;
  if (!condition) { ++counts.failures; std::cerr << "FAIL " << name << '\n'; }
}
template<class... Values> void logStub(Values...) {}
#define LOG_DBG(...) logStub(__VA_ARGS__)
#define LOG_ERR(...) logStub(__VA_ARGS__)
#define LOG_INF(...) logStub(__VA_ARGS__)
unsigned long millis() { return 0; }
struct Settings {
  bool textAntiAliasing = true;
  int getRefreshFrequency() const { return 8; }
} SETTINGS;
struct Heap {
  size_t free = 40000, largest = 20000;
  size_t getFreeHeap() const { return free; }
  size_t getMaxAllocHeap() const { return largest; }
} ESP;
bool allocationFails = false;
template<class Array> std::unique_ptr<Array> makeUniqueNoThrow(size_t size) {
  ++counts.allocations;
  return allocationFails ? nullptr : std::unique_ptr<Array>(new typename std::remove_extent<Array>::type[size]);
}
struct HalDisplay {
  enum RefreshMode { HALF_REFRESH, FAST_REFRESH };
  enum class Controller { UC8279, UC8253 };
  enum class GrayscaleMode { Absolute, Overlay };
  enum class GrayscaleBase { Separate, Combined };
  struct GrayscaleCapabilities {
    bool stripUploads = true, asyncBase = false;
    GrayscaleBase base = GrayscaleBase::Separate;
    bool supported() const { return true; }
  };
  Controller controller = Controller::UC8279;
  Controller getController() const { return controller; }
} display;
struct Gpio { bool x3 = true; bool deviceIsX3() const { return x3; } } gpio;
std::function<void()> baseHook, stripHook, waitHook, uploadHook;
struct GfxRenderer {
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  HalDisplay::GrayscaleCapabilities caps;
  RenderMode mode = BW;
  bool absolute = false;
  bool storeFails = false;
  bool stored = false;
  HalDisplay::GrayscaleCapabilities grayscaleCapabilities(HalDisplay::GrayscaleMode = HalDisplay::GrayscaleMode::Overlay) const { return caps; }
  void displayBuffer(HalDisplay::RefreshMode) const { ++counts.bw; if (baseHook) baseHook(); }
  void displayBufferAsync(HalDisplay::RefreshMode mode) const { displayBuffer(mode); }
  bool displayGrayscaleBase(HalDisplay::GrayscaleMode value, HalDisplay::RefreshMode) { absolute = value == HalDisplay::GrayscaleMode::Absolute; ++counts.base; if (baseHook) baseHook(); return true; }
  void displayGrayscaleBase(HalDisplay::RefreshMode) const { ++counts.base; if (caps.base != HalDisplay::GrayscaleBase::Combined) ++counts.bw; if (baseHook) baseHook(); }
  void waitRefreshComplete() const { if (waitHook) waitHook(); }
  void preconditionGrayscale() const {}
  void cleanupGrayscaleWithFrameBuffer() { ++counts.cleanup; ++counts.bw; mode = BW; }
  int getDisplayHeight() const { return 528; }
  int getDisplayWidthBytes() const { return 99; }
  void setRenderMode(RenderMode value) { mode = value; if (value == BW) absolute = false; }
  void clearScreen(int = 0) {}
  void beginStripTarget(uint8_t*, int, int) { if (stripHook) stripHook(); }
  void endStripTarget() {}
  void writeGrayscalePlaneStrip(bool, const uint8_t*, int, int) {}
  void displayGrayBuffer() { ++counts.gray; absolute = false; }
  bool storeBwBuffer() { ++counts.snapshot; stored = !storeFails; return stored; }
  void restoreBwBuffer() { stored = false; mode = BW; }
  void copyGrayscaleLsbBuffers() {}
  void copyGrayscaleMsbBuffers() { if (uploadHook) uploadHook(); }
};
namespace ReaderUtils {
HELPERS
}
struct Page {
  bool images = false;
  bool hasImages() const { return images; }
  bool hasImagesNeedingDecode() const { return false; }
  void renderImages(GfxRenderer&, int, int, int) { ++counts.images; }
  void renderWithImagePlaceholders(GfxRenderer&, int, int, int) {}
};
struct Popup { bool active = false; bool isActive() const { return active; } };
class EpubReaderActivity {
 public:
  enum class Overlay { None, Menu };
  Overlay overlay = Overlay::None;
  Popup overlayPopup;
  bool showBookmarkMessage = false, showDictionaryMessage = false, showIndexingMessage = false;
  bool buildPopupPending = false, buildPopupRefreshing = false, pendingSyncSaveError = false;
  bool pendingScreenshot = false, preview = false, leaving = false, jumpWaiting = false;
  std::atomic<int> pendingManualTurn{0}, pendingExternalTurn{0};
  int pagesUntilFullRefresh = 5;
  bool forcedRefreshPending = false, pageFrameKeepsUnderFast = false, bwUnderSheet = false;
  std::atomic<bool> paintDropped{false};
  GfxRenderer renderer;
  bool usesToolbarMenu() const { return true; }
  const char* pageBeingLeft() const { return leaving ? "leaving" : jumpWaiting ? "jump" : nullptr; }
  const char* nextScreenWaiting() const { return pageBeingLeft() ? pageBeingLeft() : pendingManualTurn || pendingExternalTurn ? "queued" : nullptr; }
  TextGrayContext textGrayContext() const;
  void renderStatusBar() { if (renderer.mode != GfxRenderer::BW) { if (renderer.absolute) ++counts.bwText; else ++counts.text; } }
  void drawQuoteHighlights(Page&, int, int, int) {}
  void paint(bool images = false) {
    Page storage; storage.images = images; Page* page = &storage;
    int fontId = 0, orientedMarginTop = 0, orientedMarginLeft = 0;
    unsigned long t0 = 0, tPrewarm = 0;
    auto renderPageBody = [&](bool) {
      if (renderer.mode != GfxRenderer::BW) {
        if (renderer.absolute) ++counts.bwText; else ++counts.text;
        if (page->images) ++counts.images;
      }
    };
GRAY_BODY
  }
};
CONTEXT_METHOD
class TxtReaderActivity {
 public:
  GfxRenderer renderer;
  int pagesUntilFullRefresh = 5;
  bool preview = false, covered = false;
  std::atomic<bool> leaving{false};
  std::atomic<int> pendingExternalTurn{0};
  bool coversPage() const { return covered; }
  void paint() {
    auto renderLines = [&] { if (renderer.mode != GfxRenderer::BW) ++counts.text; };
TXT_TAIL
};
void reset() {
  int failures = counts.failures; counts = {}; counts.failures = failures;
  ESP = {}; SETTINGS = {}; display = {}; gpio = {};
  baseHook = {}; stripHook = {}; waitHook = {}; uploadHook = {}; allocationFails = false;
}
int main() {
  using Field = bool TextGrayContext::*;
  for (Field field : {&TextGrayContext::covered, &TextGrayContext::preparingPopup,
                     &TextGrayContext::pendingManualTurn, &TextGrayContext::pendingExternalTurn,
                     &TextGrayContext::pageBeingLeft, &TextGrayContext::nextScreenWaiting,
                     &TextGrayContext::pendingScreenshot, &TextGrayContext::backgroundWork}) {
    TextGrayContext context; context.textAntiAliasing = true; context.*field = true;
    check(!shouldRunTextGrayscale(context), "policy rejects blocked context");
  }
  const Field fields[] = {&TextGrayContext::covered, &TextGrayContext::preparingPopup,
                         &TextGrayContext::pendingManualTurn, &TextGrayContext::pendingExternalTurn,
                         &TextGrayContext::pageBeingLeft, &TextGrayContext::nextScreenWaiting,
                         &TextGrayContext::pendingScreenshot, &TextGrayContext::backgroundWork};
  for (unsigned mask = 0; mask < 256; ++mask) for (bool enabled : {false, true}) for (bool preview : {false, true}) {
    TextGrayContext table; table.textAntiAliasing = enabled; table.preview = preview;
    for (unsigned bit = 0; bit < 8; ++bit) table.*fields[bit] = (mask & (1u << bit)) != 0;
    check(shouldRunTextGrayscale(table) == (enabled && mask == 0), "complete context truth table");
  }
  TextGrayContext context;
  check(!shouldRunTextGrayscale(context), "AA disabled");
  context.textAntiAliasing = true;
  check(shouldRunTextGrayscale(context), "visible page");
  context.preview = true;
  check(shouldRunTextGrayscale(context), "Home preview is reading");
  reset(); EpubReaderActivity reader;
  reader.overlay = EpubReaderActivity::Overlay::Menu;
  reader.paint(); reader.paint(); reader.paint();
  check(counts.allocations == 0 && counts.gray == 0, "menu and tab changes allocate zero");
  reader.overlay = EpubReaderActivity::Overlay::None; reader.paint();
  check(counts.allocations == 1 && counts.gray == 1, "menu close keeps one gray pass");
  for (bool EpubReaderActivity::*field : {&EpubReaderActivity::showBookmarkMessage,
       &EpubReaderActivity::showDictionaryMessage, &EpubReaderActivity::showIndexingMessage,
       &EpubReaderActivity::buildPopupPending, &EpubReaderActivity::buildPopupRefreshing,
       &EpubReaderActivity::pendingSyncSaveError, &EpubReaderActivity::pendingScreenshot}) {
    reset(); EpubReaderActivity notice; notice.*field = true; notice.paint();
    check(counts.allocations == 0 && counts.gray == 0, "popup or capture allocates zero");
    notice.*field = false; notice.paint();
    check(counts.allocations == 1 && counts.gray == 1, "popup dismissal keeps one gray pass");
  }
  for (bool buffered : {false, true}) for (int state = 0; state < 4; ++state) {
    reset(); EpubReaderActivity moving;
    moving.renderer.caps.asyncBase = buffered;
    if (buffered) { ESP.free = 250000; ESP.largest = 200000; }
    baseHook = [&] {
      if (state == 0) moving.pendingManualTurn = 3;
      if (state == 1) moving.pendingExternalTurn = 2;
      if (state == 2) moving.leaving = true;
      if (state == 3) moving.jumpWaiting = true;
    };
    moving.paint();
    check(counts.allocations == 0 && counts.gray == 0, "turn queued after BW allocates zero");
    check(moving.pendingManualTurn == (state == 0 ? 3 : 0) && moving.pendingExternalTurn == (state == 1 ? 2 : 0), "accepted input stays intact");
  }
  for (bool buffered : {false, true}) {
    reset(); EpubReaderActivity moving;
    moving.renderer.caps.asyncBase = buffered;
    if (buffered) { ESP.free = 250000; ESP.largest = 200000; }
    stripHook = [&] { moving.pendingExternalTurn = 2; };
    moving.paint();
    check(counts.gray == 0 && counts.cleanup == 1, "mid-plane turn cancels waveform and cleans base");
    check(moving.pendingExternalTurn == 2, "mid-plane turn preserves queue");
  }
  for (bool combined : {false, true}) for (int low = 0; low < 2; ++low) {
    reset(); EpubReaderActivity lowHeap;
    if (combined) lowHeap.renderer.caps.base = HalDisplay::GrayscaleBase::Combined;
    if (low == 0) ESP.free = 10239; else ESP.largest = 8179;
    lowHeap.paint();
    check(counts.allocations == 0 && counts.gray == 0 && counts.bw >= 1, "heap floor keeps BW with zero gray allocations");
  }
  // A block of exactly 8,192 B reports 8,180 with light poisoning and serves the 7,920 B strip.
  reset(); ESP.free = 10240; ESP.largest = 8180; EpubReaderActivity edge; edge.paint();
  check(counts.allocations == 1 && counts.gray == 1, "exact pre-scratch floor admitted");
  reset(); EpubReaderActivity preview; preview.preview = true; preview.paint();
  check(counts.allocations == 1 && counts.gray == 1, "Home preview gets one pass");
  reset(); EpubReaderActivity stable; stable.paint();
  check(counts.allocations == 1 && counts.gray == 1, "stationary reader gets one pass");
  reset(); EpubReaderActivity imageSheet; imageSheet.overlay = EpubReaderActivity::Overlay::Menu; imageSheet.paint(true);
  check(counts.text == 0 && counts.images > 0 && counts.gray == 1 && counts.allocations == 1, "image under sheet keeps image gray only");
  reset(); gpio.x3 = false; EpubReaderActivity absoluteSheet;
  absoluteSheet.renderer.caps.stripUploads = false;
  absoluteSheet.overlay = EpubReaderActivity::Overlay::Menu; absoluteSheet.paint(true);
  check(counts.text == 0 && counts.bwText > 0 && counts.images > 0 && counts.gray == 1,
        "absolute image under sheet keeps BW glyphs with no text AA");
  reset(); ESP.free = 10239; EpubReaderActivity imageFloor; imageFloor.paint(true);
  check(counts.allocations == 0 && counts.gray == 0, "image uses same scratch floor");
  for (bool tiled : {false, true}) {
    reset(); EpubReaderActivity oom; oom.renderer.caps.stripUploads = tiled;
    allocationFails = true; oom.renderer.storeFails = true; oom.paint();
    check(counts.gray == 0 && oom.renderer.mode == GfxRenderer::BW, "OOM keeps BW");
  }
  for (bool tiled : {false, true}) {
    reset(); EpubReaderActivity clean; clean.renderer.caps.stripUploads = tiled; clean.paint();
    check(counts.gray == 1, "both plane paths keep stationary gray");
    reset(); EpubReaderActivity blocked; blocked.renderer.caps.stripUploads = tiled;
    blocked.showBookmarkMessage = true; blocked.paint();
    check(counts.allocations == 0 && counts.snapshot == 0 && counts.gray == 0, "both plane paths block popup");
  }
  for (int state = 0; state < 5; ++state) {
    reset(); TxtReaderActivity text;
    text.preview = state == 0; text.covered = state == 1;
    text.leaving = state == 2; text.pendingExternalTurn = state == 3;
    if (state == 4) ESP.free = 10239;
    text.paint();
    check(counts.gray == (state == 0 ? 1 : 0) && counts.snapshot == (state == 0 ? 1 : 0), "TXT caller policy and heap");
  }
  reset(); EpubReaderActivity lateEpub; lateEpub.renderer.caps.stripUploads = false;
  uploadHook = [&] { lateEpub.pendingExternalTurn = 2; }; lateEpub.paint();
  check(counts.gray == 0 && !lateEpub.renderer.stored && lateEpub.pendingExternalTurn == 2,
        "EPUB input after MSB upload cancels waveform and restores BW");
  reset(); TxtReaderActivity lateTxt;
  uploadHook = [&] { lateTxt.pendingExternalTurn = 2; }; lateTxt.paint();
  check(counts.gray == 0 && !lateTxt.renderer.stored && lateTxt.pendingExternalTurn == 2,
        "TXT input after MSB upload cancels waveform and restores BW");
DIRECT_HELPER_TEST
  std::cout << "checks=" << checks << " failures=" << counts.failures << '\n';
  return counts.failures ? 1 : 0;
}
'''
for marker, value in [('POLICY_INCLUDE', policy_include), ('HELPERS', helper), ('GRAY_BODY', gray_body),
                      ('CONTEXT_METHOD', context_method), ('TXT_TAIL', txt_tail)]:
    harness = harness.replace(marker, value)
direct_helper_test = r'''
  reset(); GfxRenderer direct; TextGrayContext blockedContext;
  blockedContext.textAntiAliasing = true; blockedContext.covered = true;
  ReaderUtils::renderAntiAliased(direct, [] {}, [&] { return blockedContext; });
  check(counts.snapshot == 0 && counts.gray == 0, "direct helper cannot bypass policy");
  reset(); GfxRenderer late; TextGrayContext liveContext; liveContext.textAntiAliasing = true;
  ReaderUtils::renderAntiAliased(late, [&] { liveContext.pendingExternalTurn = true; }, [&] { return liveContext; });
  check(counts.snapshot == 1 && counts.gray == 0 && !late.stored && late.mode == GfxRenderer::BW,
        "TXT mid-plane input restores BW without waveform");
''' if policy.exists() else r'''
  reset(); GfxRenderer direct;
  ReaderUtils::renderAntiAliased(direct, [] {});
  check(counts.snapshot == 0 && counts.gray == 0, "direct helper cannot bypass policy");
'''
harness = harness.replace('DIRECT_HELPER_TEST', direct_helper_test)
output = args.out / 'harness.cpp'
output.write_text(harness)
binary = args.out / 'text-gray-policy'
subprocess.run(['c++', '-std=c++20', '-I', str(reader), '-I', str(args.repo / 'lib/Memory'), str(output), '-o', str(binary)],
               check=True)
subprocess.run([str(binary)], check=True)
