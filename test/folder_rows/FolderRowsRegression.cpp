#include <FreeInkApp.h>
#include <FsHelpers.h>
#include <Utf8.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace fui = freeink::ui;
using UiScreen = fui::Screen<24>;

// Allocation instrumentation counts C++ allocations made by production row
// construction. Raw filenames exist before the measured derived-row phase.
static size_t liveBytes = 0, peakBytes = 0, allocationCount = 0, largestAllocation = 0;
struct alignas(std::max_align_t) AllocationHeader { size_t size; };
void* operator new(size_t size) {
  auto* memory = static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader) + size));
  if (!memory) throw std::bad_alloc();
  memory->size = size;
  liveBytes += size;
  peakBytes = std::max(peakBytes, liveBytes);
  largestAllocation = std::max(largestAllocation, size);
  ++allocationCount;
  return memory + 1;
}
void operator delete(void* memory) noexcept {
  if (!memory) return;
  auto* header = static_cast<AllocationHeader*>(memory) - 1;
  liveBytes -= header->size;
  std::free(header);
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }

static unsigned checks = 0, failures = 0, partialChecks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; \
  std::printf("FAIL line=%d %s\n", __LINE__, #expr); } } while (false)

struct CrossPointSettings { static constexpr int TENOR_UI = 4; };
struct Settings { int uiTheme = 4; bool globalStatusBarHidden() const { return true; } } SETTINGS;
struct Metrics { int topPadding = 0, headerHeight = 80, buttonHintsHeight = 30,
  verticalSpacing = 8, contentSidePadding = 12, listWithSubtitleRowHeight = 36, listRowHeight = 36; };
struct UITheme {
  bool icons = true;
  static UITheme& getInstance() { static UITheme theme; return theme; }
  UITheme& getTheme() { return *this; }
  bool showsFileIcons() const { return icons; }
  const Metrics& getMetrics() const { static Metrics metrics; return metrics; }
  static int getFileIcon(const std::string&) { return 0; }
};
static fui::BitmapRef listIconFor(int, int) { return {}; }
struct Scale { int bodyFontId = 1, smallFontId = 0; };
static Scale uiScaleSpec() { return {}; }
static fui::TextStyle uiMenuLabelText(const fui::ThemeTokens& theme) { return theme.bodyText; }
static constexpr int SMALL_FONT_ID = 0;
enum { STR_FILE_SIDE_ACTIONS, STR_NO_BIN_FILES, STR_NO_FILES_FOUND };
static const char* tr(int) { return "empty"; }
namespace tenorchrome { int tipHeight(...) { return 0; } }

struct Renderer {
  size_t prewarmCalls = 0, prewarmNames = 0, prewarmBytes = 0;
  int getLineHeight(int) const { return 16; }
  int getTextWidth(int, const char* text) const { return std::strlen(text) * 4; }
  void drawText(int, int, int, const char*) const {}
  void prewarmFallbackText(int, const char* (*get)(const void*, uint32_t), const void* context, uint32_t count) {
    ++prewarmCalls;
    prewarmNames += count;
    for (uint32_t i = 0; i < count; ++i) prewarmBytes += std::strlen(get(context, i));
  }
};

static int16_t drawLineHeight = 16;
struct Draw : fui::DrawTarget {
  mutable size_t labels = 0;
  char lastLabel[512]{};
  uint8_t lastMaxLines = 0;
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text) * 4), drawLineHeight};
  }
  int16_t lineHeight(fui::FontId) const override { return drawLineHeight; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char* text, fui::TextStyle style) override {
    if (text) {
      labels += std::strlen(text);
      std::snprintf(lastLabel, sizeof(lastLabel), "%s", text);
      lastMaxLines = style.maxLines;
    }
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};

std::string getFileName(std::string filename);
std::string getFileExtension(const std::string& filename);
class FileBrowserActivity {
 public:
  enum class Mode { Books, PickFirmware };
  Mode mode = Mode::Books;
  std::string basepath = "/測試";
  std::vector<std::string> files, rowNames, rowExtensions;
  std::vector<fui::ListItem> rowItems;
  bool rowsUseFileIcons = false;
  int rowWindowFirst = -1;
  Renderer renderer;
  fui::ListNav nav;
  struct { bool hasTouch() const { return false; } } mappedInput;
  int pinned = -1;
  static constexpr int ACTION_ROW = 1;
  int listCount() const { return static_cast<int>(files.size()); }
#if WINDOWED
  void rebuildRowItems(const int first, const int count);
#else
  void rebuildRowItems();
#endif
  void buildScreen(UiScreen& screen);
  void syncListViewport(UiScreen& screen, fui::ListProps& props, bool hasSubtitle = false);
  void decoratePinnedRows(fui::ListProps&);
  void reserveFixedMenuContent(UiScreen&) {}
  void reserveFavoriteHint(UiScreen&) {}
  fui::ListNav& activeNav() { return nav; }
  static int kepConTro(int selected, int count) { return std::clamp(selected, 0, std::max(0, count - 1)); }
  bool rowIsPinned(int index) const { return index == pinned; }
  void invalidate() {
    rowWindowFirst = -1;
#if !WINDOWED
    rebuildRowItems();
#endif
  }
};

#include "production_rows.inc"

static fui::ThemeTokens theme;
static void render(FileBrowserActivity& browser, int top, bool assertBound = true, bool follow = false,
                   bool verify = true) {
  Draw draw;
  fui::DeviceContext device;
  device.width = 528;
  device.height = 792;
  fui::InputSnapshot input;
  fui::InteractionBuffer<24> interactions;
  fui::Frame<24> frame(draw, device, input, interactions);
  browser.nav.top = top;
  browser.nav.selected = top;
  browser.nav.followOnBuild = false;
  browser.pinned = top;
  size_t finalPins = 0;
  if (follow) {
    browser.nav.top = std::max(0, top - browser.nav.visibleRows + 1);
    browser.nav.follow(browser.listCount());
  }
  // Wrapped labels can refine the viewport. Exercise the actual SDK list
  // against the production materialized labels and absolute action values.
  for (int pass = 0; pass < 32; ++pass) {
    interactions.clear();
    UiScreen screen(frame, theme);
    browser.buildScreen(screen);
    finalPins = pinDecorationCount;
    if (verify) CHECK(pinDecorationCount <= 1);
    if (verify && pinDecorationCount) {
      CHECK(pinDecorations[0].row->actionValue == browser.pinned);
      CHECK(std::strncmp(pinDecorations[0].row->label, "\xEE\x84\x8A", 3) == 0);
    }
    restorePinnedRows();
    if (!browser.nav.consumeRebuildNeeded()) break;
    CHECK(pass < 31);
  }
  if (!verify) return;
  CHECK(finalPins == 1);
  if (draw.lastMaxLines == 1) {
    const int preview = browser.nav.top + browser.nav.drawnRows;
    CHECK(preview < browser.listCount());
    CHECK(std::string(draw.lastLabel) == getFileName(browser.files[preview]));
    ++partialChecks;
  }
  const int first = WINDOWED ? browser.rowWindowFirst : 0;
  for (size_t i = 0; i < browser.rowItems.size(); ++i) {
    CHECK(browser.rowItems[i].actionValue == static_cast<int>(first + i));
    CHECK(std::string(browser.rowItems[i].label) == getFileName(browser.files[first + i]));
    CHECK(browser.rowItems[i].value == nullptr ||
          std::string(browser.rowItems[i].value) == getFileExtension(browser.files[first + i]));
  }
  CHECK(interactions.count() > 0);
  CHECK(interactions.data()[0].value == browser.nav.top);
  bool selectionDrawn = false;
  for (size_t i = 0; i < interactions.count(); ++i) {
    const auto& interaction = interactions.data()[i];
    CHECK(interaction.value >= browser.nav.top);
    CHECK(interaction.value < browser.listCount());
    selectionDrawn |= interaction.value == top;
    // Hit testing must emit the global file index, including the last page.
    fui::InputSnapshot tap;
    tap.touchReleased = true;
    tap.touchX = interaction.rect.x + 1;
    tap.touchY = interaction.rect.y + 1;
    const auto event = interactions.route(tap);
    CHECK(event.value == interaction.value);
  }
  CHECK(selectionDrawn);
  CHECK(!browser.nav.followPending);
  if (assertBound) CHECK(browser.rowItems.size() <= static_cast<size_t>(browser.nav.visibleRows + 1));
}

int main() {
  theme.rowHeight = 36;
  theme.listRowGap = 0;
  for (const int total : {100, 1000, 5000}) {
    FileBrowserActivity browser;
    for (int i = total - 1; i >= 0; --i)
      browser.files.emplace_back("測試書名-長長的檔案名稱-" + std::to_string(i) + "-a\xcc\x81.epub");
    const auto sortStart = std::chrono::steady_clock::now();
    FsHelpers::sortFileList(browser.files);
    const auto sortMicros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - sortStart).count();
    const size_t base = liveBytes;
    peakBytes = base;
    allocationCount = largestAllocation = 0;
    const auto start = std::chrono::steady_clock::now();
    browser.invalidate();
    render(browser, 0, false, false, false);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::printf("MEASURE count=%d derived_rows=%zu retained_bytes=%zu peak_bytes=%zu allocations=%zu "
                "largest_allocation=%zu prewarm_names=%zu prewarm_bytes=%zu first_paint_us=%lld sort_us=%lld\n",
                total, browser.rowItems.size(), liveBytes - base, peakBytes - base, allocationCount,
                largestAllocation, browser.renderer.prewarmNames, browser.renderer.prewarmBytes,
                static_cast<long long>(micros), static_cast<long long>(sortMicros));
    CHECK(browser.rowItems.size() <= static_cast<size_t>(browser.nav.visibleRows + 1));
    CHECK(browser.renderer.prewarmNames <= static_cast<size_t>(browser.nav.visibleRows + 2));
    const auto prewarmed = browser.renderer.prewarmCalls;
    render(browser, 0);
    CHECK(browser.renderer.prewarmCalls == prewarmed);
    render(browser, total / 2);
    render(browser, total - 1, true, true);
    CHECK(browser.nav.selected == total - 1);
    render(browser, 0);
    UITheme::getInstance().icons = false;
    browser.files = {"a\xcc\x81/", "測試1.epub", "測試2.epub"};
    browser.invalidate();
    render(browser, 0);
    CHECK(std::string(browser.rowItems.front().label) == "[á]");
    UITheme::getInstance().icons = true;
    render(browser, 0);
    CHECK(std::string(browser.rowItems.front().label) == "á");
  }
  // Names long enough to actually wrap at the viewport width. Follow a
  // selection near the bottom, forcing ListNav to refine its fixed-height
  // estimate and rebuild until the last selected global index is drawn.
  FileBrowserActivity wrapped;
  drawLineHeight = 24;
  for (int i = 0; i < 100; ++i) {
    std::string name = "long-" + std::to_string(i) + "-";
    for (int j = 0; j < 22; ++j) name += "測試書名";
    wrapped.files.push_back(name + ".epub");
  }
  wrapped.invalidate();
  render(wrapped, 0);
  CHECK(wrapped.nav.drawnRows < wrapped.nav.visibleRows);
  render(wrapped, 99, true, true);
  render(wrapped, 50, true, true);
  CHECK(partialChecks > 0);
  std::printf("RESULT checks=%u failures=%u windowed=%d\n", checks, failures, WINDOWED);
  return failures ? 1 : 0;
}
