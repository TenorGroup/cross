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

// Allocation instrumentation counts C++ allocations made while the production
// screen draws its rows. Raw filenames exist before the measured phase.
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

struct Settings { bool globalStatusBarHidden() const { return true; } } SETTINGS;
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
  // Every label the list drew this pass, in draw order (names and extensions).
  std::vector<std::string> drawn;
  // Clipping, as the device's GfxRenderer target supports: the list draws the
  // partial preview row only on a target that can clip it at the fold.
  static constexpr fui::Rect NO_CLIP{0, 0, 32767, 32767};
  fui::Rect clip = NO_CLIP;
  std::vector<std::string> clippedLabels;
  fui::Rect clipRect() const override { return clip; }
  bool setClipRect(fui::Rect rect) override {
    clip = rect;
    return true;
  }
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text) * 4), drawLineHeight};
  }
  int16_t lineHeight(fui::FontId) const override { return drawLineHeight; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char* text, fui::TextStyle) override {
    if (text) {
      drawn.emplace_back(text);
      const bool clipped = clip.x != NO_CLIP.x || clip.y != NO_CLIP.y || clip.width != NO_CLIP.width ||
                           clip.height != NO_CLIP.height;
      if (clipped) clippedLabels.emplace_back(text);
      labels += std::strlen(text);
    }
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};

std::string getFileExtension(const std::string& filename);
void formatFileName(const std::string& filename, char* buffer, size_t bufferSize);
void formatFileExtension(const std::string& filename, char* buffer, size_t bufferSize);
class FileBrowserActivity {
 public:
  enum class Mode { Books, PickFirmware };
  Mode mode = Mode::Books;
  std::string basepath = "/測試";
  std::vector<std::string> files;
  static constexpr size_t ROW_NAME_BUF_SIZE = 512;
  char rowNameBuf[ROW_NAME_BUF_SIZE]{};
  char rowExtBuf[16]{};
  static void provideRow(void* ctx, uint16_t index, fui::ListItem& item);
  static constexpr int PREWARM_WINDOW = 24;
  int prewarmedStart = -1;
  void prewarmRowGlyphs(int start);
  Renderer renderer;
  fui::ListNav nav;
  struct { bool hasTouch() const { return false; } } mappedInput;
  int pinned = -1;
  static constexpr int ACTION_ROW = 1;
  int listCount() const { return static_cast<int>(files.size()); }
  void buildScreen(UiScreen& screen);
  void syncListViewport(UiScreen& screen, fui::ListProps& props, bool hasSubtitle = false);
  void decoratePinnedRows(fui::ListProps&);
  void reserveFixedMenuContent(UiScreen&) {}
  void reserveFavoriteHint(UiScreen&) {}
  void reserveMoreBelowChevron(UiScreen&, int16_t, int) {}
  fui::ListNav& activeNav() { return nav; }
  static int kepConTro(int selected, int count) { return std::clamp(selected, 0, std::max(0, count - 1)); }
  bool rowIsPinned(int index) const { return index == pinned; }
  // A new folder listing, as loadFiles() does: re-prewarm the visible window.
  void invalidate() { prewarmedStart = -1; }
};

// The value in src/components/UIThemeTokens.h, which the extracted syncListViewport reads.
static constexpr int16_t TENOR_PILL_ROW_PADDING_Y = 7;

#include "production_rows.inc"

static const char PIN_GLYPH[] = "\xEE\x84\x8A";
// The label a row must show, from the production formatter.
static std::string expectedLabel(const FileBrowserActivity& browser, int index) {
  char name[FileBrowserActivity::ROW_NAME_BUF_SIZE];
  formatFileName(browser.files[index], name, sizeof(name));
  return browser.rowIsPinned(index) ? std::string(PIN_GLYPH) + name : std::string(name);
}
static bool wasDrawn(const Draw& draw, const std::string& label) {
  return std::find(draw.drawn.begin(), draw.drawn.end(), label) != draw.drawn.end();
}

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
  if (follow) {
    browser.nav.top = std::max(0, top - browser.nav.visibleRows + 1);
    browser.nav.follow(browser.listCount());
  }
  // Wrapped labels can refine the viewport. Exercise the actual SDK list
  // against the production row provider and absolute action values.
  for (int pass = 0; pass < 32; ++pass) {
    interactions.clear();
    draw.drawn.clear();
    draw.clippedLabels.clear();
    UiScreen screen(frame, theme);
    browser.buildScreen(screen);
    // Rows come from the provider, so no ListItem array is left to patch.
    if (verify) CHECK(pinDecorationCount == 0);
    restorePinnedRows();
    if (!browser.nav.consumeRebuildNeeded()) break;
    CHECK(pass < 31);
  }
  if (!verify) return;
  // The pinned row (Home Favourites) shows its glyph, exactly once.
  size_t pins = 0;
  for (const auto& label : draw.drawn) pins += label.compare(0, 3, PIN_GLYPH) == 0;
  CHECK(pins == 1);
  CHECK(wasDrawn(draw, expectedLabel(browser, browser.pinned)));
  // The row past the fold is drawn clipped, as a preview of what comes next.
  if (!draw.clippedLabels.empty()) {
    const int preview = browser.nav.top + browser.nav.drawnRows;
    CHECK(preview < browser.listCount());
    CHECK(std::find(draw.clippedLabels.begin(), draw.clippedLabels.end(), expectedLabel(browser, preview)) !=
          draw.clippedLabels.end());
    ++partialChecks;
  }
  CHECK(interactions.count() > 0);
  CHECK(interactions.data()[0].value == browser.nav.top);
  bool selectionDrawn = false;
  for (size_t i = 0; i < interactions.count(); ++i) {
    const auto& interaction = interactions.data()[i];
    CHECK(interaction.value >= browser.nav.top);
    CHECK(interaction.value < browser.listCount());
    selectionDrawn |= interaction.value == top;
    // Every full row shows its own file's name and extension.
    CHECK(wasDrawn(draw, expectedLabel(browser, interaction.value)));
    const std::string extension = getFileExtension(browser.files[interaction.value]);
    CHECK(extension.empty() || wasDrawn(draw, extension));
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
  (void)assertBound;
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
    std::printf("MEASURE count=%d retained_bytes=%zu peak_bytes=%zu allocations=%zu "
                "largest_allocation=%zu prewarm_names=%zu prewarm_bytes=%zu first_paint_us=%lld sort_us=%lld\n",
                total, liveBytes - base, peakBytes - base, allocationCount,
                largestAllocation, browser.renderer.prewarmNames, browser.renderer.prewarmBytes,
                static_cast<long long>(micros), static_cast<long long>(sortMicros));
    // Nothing per file stays behind after a paint, whatever the folder size.
    CHECK(liveBytes == base);
    // One bounded prewarm window plus the path band, never the whole folder.
    CHECK(browser.renderer.prewarmNames <=
          static_cast<size_t>(std::min(total, FileBrowserActivity::PREWARM_WINDOW) + 1));
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
    render(browser, 1);
    fui::ListItem folder;
    FileBrowserActivity::provideRow(&browser, 0, folder);
    CHECK(std::string(folder.label) == "[á]");
    CHECK(folder.value == nullptr);
    UITheme::getInstance().icons = true;
    render(browser, 1);
    FileBrowserActivity::provideRow(&browser, 0, folder);
    CHECK(std::string(folder.label) == "á");
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
  std::printf("RESULT checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
