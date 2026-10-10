#include <algorithm>
#include <array>
#include <iostream>
#include <string>

#include "FreeInkUI.h"
#include "FreeInkUIIcon.h"
#include "FreeInkApp.h"
#include "Icon.h"
#include "components/icons/tenorHomeTabIcons.h"
#include "components/icons/tenorReaderTabIcons.h"
#include "components/icons/readerToolbarTabIcons.h"
#include "shells/ugly/UglyMarkSeed.h"
#include "shells/ugly/UglyIcons.h"
#include "activities/reader/ReaderMenuLayout.h"
#if __has_include("components/icons/X3ReaderTabIcons.h")
#include "components/icons/X3ReaderTabIcons.h"
#endif
#if __has_include("components/icons/tenorTextTabIcons.h")
#include "components/icons/tenorTextTabIcons.h"
#endif

namespace fui = freeink::ui;

class GfxRenderer {
 public:
  mutable std::array<unsigned char, 528 * 80> pixels;
  GfxRenderer() { pixels.fill(255); }
  void drawPixel(int x, int y, bool black) const {
    if (x >= 0 && x < 528 && y >= 0 && y < 80) pixels[y * 528 + x] = black ? 0 : 255;
  }
};

namespace tenorchrome {
#if FREEINK_DEVICE_X4PRO
constexpr bool kTouchShell = true;
#else
constexpr bool kTouchShell = false;
#endif
constexpr int BAR_TAB_W = 84;
constexpr int BAR_TAB_INSET = 6;
constexpr int BAR_END_INSET = 8;
constexpr int X3_BAR_MARGIN = 16;
bool x3ScreenLayoutEnabled() { return !kTouchShell; }
void drawPillRing(const GfxRenderer&, int, int, int, int, int, bool);
void drawBarFrame(const GfxRenderer&, int, int, int, int);
void drawBarIcon(const GfxRenderer&, const uint8_t*, int, int, int, int, bool);
void drawBarTab(const GfxRenderer&, int, int, int, const uint8_t*, int, int, bool);
}

class UiScreen {
 public:
  struct Target {
    fui::Size measureText(fui::FontId, const char*, const fui::TextStyle&) { return {}; }
    void text(fui::Rect, const char*, const fui::TextStyle&) {}
    void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) {}
  } painter;
  struct Frame {
    int hits = 0;
    void hit(fui::Rect, int, int, int) { ++hits; }
  } zones;
  Target& target() { return painter; }
  Frame& frame() { return zones; }
  fui::Rect take(fui::LayoutAnchor, int16_t height) { return {0, 0, 528, height}; }
};

namespace shell { bool handwritten = false; bool uglyParts() { return handwritten; } bool isUgly() { return handwritten; } }
namespace ugly {
uint32_t iconFrameSeed() { return 0; }
bool iconAsset(const GfxRenderer&, const uint8_t*, icons::Rect, uint32_t) { return false; }
}
namespace uglychrome {
fui::Rect words(const GfxRenderer&, fui::Rect rect, const char*, fui::TextAlign) { return rect; }
void ring(const GfxRenderer&, fui::Rect, uint32_t = 0) {}
}

class UiTabListActivity {
 public:
  GfxRenderer renderer;
  mutable ugly::MarkState tabMark_;
  int activeTab() const { return 0; }
  struct PaintSwitch { bool paintingEnabled() const { return true; } } uiTarget;
  static constexpr int ACTION_TAB = 1;
  void veThanhTheTenor(UiScreen&, const fui::Rect&, const fui::TabItem*, int, const fui::TextStyle&);
};

class HomeActivity {
 public:
  static constexpr int TAB_COUNT = 5;
  fui::BitmapRef tabIcon(int, bool) const;
};

class TextSettingsActivity {
 public:
  enum class Tab : uint8_t { Family, Size, Layout, Style, Favorites, Count };
  int tabCount() const { return tenorchrome::x3ScreenLayoutEnabled() ? 5 : 4; }
  Tab tabForIndex(int) const;
  fui::BitmapRef tabIcon(int, bool) const;
};

class EpubReaderMenuActivity {
 public:
  using MenuTab = readermenu::Tab;
  bool textOnly = false;
  int tabCount() const { return textOnly ? 1 : tenorchrome::x3ScreenLayoutEnabled() ? 5 : 4; }
  fui::BitmapRef tabIcon(int, bool) const;
};

constexpr int16_t kToolRowH = 80;
constexpr int ACTION_TOOL = 1;

class ReaderToolbarUi {
 public:
  static constexpr int kToolCount = 4;
  GfxRenderer* renderer_;
  struct Model { int activeTool; } model_;
  UiTabListActivity::PaintSwitch uiTarget;
  void buildToolRow(UiScreen&, fui::LayoutAnchor, int16_t);
};

#include "Production.inc"

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  std::string kind = argv[1];
  if (kind.rfind("ugly-", 0) == 0) {
    shell::handwritten = true;
    kind.erase(0, 5);
  }
  const int chosen = std::stoi(argv[2]);
  UiTabListActivity bar;
  if (kind == "reader-legacy") {
    EpubReaderMenuActivity reader;
    for (const bool selected : {false, true}) {
      const freeink::Icon* icons[] = {
          selected ? &icon_tenor_home_favorites_bold_40 : &icon_tenor_home_favorites_40,
          selected ? &icon_tenor_reader_position_bold_40 : &icon_tenor_reader_position_40,
          selected ? &icon_tenor_reader_reading_bold_40 : &icon_tenor_reader_reading_40,
          selected ? &icon_tenor_reader_tools_bold_40 : &icon_tenor_reader_tools_40,
          selected ? &icon_tenor_reader_reading_bold_40 : &icon_tenor_reader_reading_40};
      if (!tenorchrome::kTouchShell)
        icons[2] = selected ? &icon_tenor_home_recent_bold_40 : &icon_tenor_home_recent_40;
      for (int index = 0; index < reader.tabCount(); ++index)
        if (reader.tabIcon(index, selected).data != icons[index]->bits) return 5;
      reader.textOnly = true;
      if (reader.tabIcon(0, selected).data != icons[tenorchrome::kTouchShell ? 0 : 4]->bits) return 6;
      reader.textOnly = false;
    }
  } else if (kind == "raw" || kind == "tab") {
    std::array<uint8_t, 200> bits;
    std::cin.read(reinterpret_cast<char*>(bits.data()), bits.size());
    if (std::cin.gcount() != bits.size()) return 3;
    if (kind == "raw")
      tenorchrome::drawBarIcon(bar.renderer, bits.data(), 40, 40, 0, 0, chosen);
    else
      tenorchrome::drawBarTab(bar.renderer, 160, 0, 60, bits.data(), 40, 40, chosen);
  } else if (kind == "toolbar") {
    ReaderToolbarUi toolbar;
    toolbar.renderer_ = &bar.renderer;
    toolbar.model_.activeTool = chosen;
    UiScreen screen;
    toolbar.buildToolRow(screen, {}, 0);
    if (screen.zones.hits != 4) return 4;
  } else {
    const HomeActivity home;
    const TextSettingsActivity text;
    const EpubReaderMenuActivity reader;
    std::array<fui::TabItem, 5> tabs;
    constexpr std::array<int, 5> homeOrder = {0, 1, 4, 2, 3};
    constexpr std::array<int, 5> textOrder = {4, 0, 1, 2, 3};
    const int count = kind == "reader" ? reader.tabCount() : 5;
    for (int index = 0; index < count; ++index) {
      tabs[index].selected = index == chosen;
      if (kind == "home") {
        tabs[index].icon = home.tabIcon(homeOrder[index], tabs[index].selected);
      } else if (kind == "text") {
        tabs[index].icon = text.tabIcon(textOrder[index], tabs[index].selected);
      } else
        tabs[index].icon = reader.tabIcon(index, tabs[index].selected);
    }
    UiScreen screen;
    const fui::Rect rect = tenorchrome::kTouchShell ? fui::Rect{16, 0, 496, 60} : fui::Rect{0, 0, 528, 60};
    bar.veThanhTheTenor(screen, rect, tabs.data(), count, {});
    if (screen.zones.hits != (tenorchrome::kTouchShell ? count : 0)) return 4;
  }
  const int height = kind == "toolbar" ? 80 : 60;
  std::cout.write(reinterpret_cast<const char*>(bar.renderer.pixels.data()), 528 * height);
}
