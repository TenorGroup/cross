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
bool x3ScreenLayoutEnabled() { return !kTouchShell; }
void drawPillRing(const GfxRenderer&, int, int, int, int, int, bool);
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

namespace shell { bool uglyParts() { return false; } bool isUgly() { return false; } }
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

namespace readermenu {
constexpr int TAB_COUNT = 4;
enum class Tool : uint8_t { FAVORITES, CONTENTS, TEXT, MORE };
}

class EpubReaderMenuActivity {
 public:
  enum class MenuTab : uint8_t { FAVORITES, POSITION, READING, TOOLS, TEXT };
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
  const std::string kind = argv[1];
  const int chosen = std::stoi(argv[2]);
  UiTabListActivity bar;
  if (kind == "raw" || kind == "tab") {
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
