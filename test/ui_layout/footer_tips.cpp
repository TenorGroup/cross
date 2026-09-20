#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <builtinFonts/bevietnampro_8_regular.h>
#include <builtinFonts/bevietnampro_10_regular.h>
#include <builtinFonts/geist_12_regular.h>

constexpr int SMALL_FONT_ID = 1;
namespace EpdFontFamily { enum Style { REGULAR }; }
struct Settings {
  int uiTextSize = 0;
  bool hidden = false, large = false, tenorButtonSymbols = true;
  bool globalStatusBarHidden() const { return hidden; }
  bool globalStatusBarLarge() const { return large; }
} SETTINGS;
int normalizedUiTextSize(int value) { return value; }
struct GPIO { bool hasTouch() const { return false; } } gpio;
struct Metrics { int buttonHintsHeight; };
struct UITheme {
  static UITheme& getInstance() { static UITheme t; return t; }
  Metrics getMetrics() const { return {SETTINGS.hidden ? 0 : (SETTINGS.uiTextSize == 0 ? 40 : SETTINGS.uiTextSize == 1 ? 80 : 90)}; }
};
enum class Color { LightGray };
struct GfxRenderer {
  enum Orientation { Portrait };
  enum { BW };
  mutable std::vector<unsigned char> pixels = std::vector<unsigned char>(480 * 800);
  mutable std::vector<int> ys;
  const EpdFontData& font() const {
    return SETTINGS.uiTextSize == 0 ? bevietnampro_8_regular : SETTINGS.uiTextSize == 1 ? bevietnampro_10_regular : geist_12_regular;
  }
  const EpdGlyph* glyph(unsigned cp) const {
    const auto& f = font();
    for (unsigned i = 0; i < f.intervalCount; ++i) {
      const auto& v = f.intervals[i];
      if (cp >= v.first && cp <= v.last) return f.glyph + v.offset + cp - v.first;
    }
    return nullptr;
  }
  static unsigned next(const char*& text) {
    unsigned cp = static_cast<unsigned char>(*text++);
    if (cp < 128) return cp;
    int n = cp < 224 ? 1 : cp < 240 ? 2 : 3;
    cp &= (1 << (6 - n)) - 1;
    while (n--) cp = (cp << 6) | (static_cast<unsigned char>(*text++) & 63);
    return cp;
  }
  int getScreenHeight() const { return 800; }
  int getScreenWidth() const { return 480; }
  int getLineHeight(int) const { return font().advanceY; }
  int getFontAscenderSize(int) const { return font().ascender; }
  int getTextHeight(int) const { return font().ascender; }
  int getTextWidth(int, const char* text) const {
    int width = 0;
    while (*text) if (const auto* g = glyph(next(text))) width += g->advanceX / 16;
    return width;
  }
  int getTextInkBottom(int, const char* text, EpdFontFamily::Style) const {
    int bottom = 0;
    while (*text) if (const auto* g = glyph(next(text)); g && g->width && g->height)
      bottom = std::max(bottom, font().ascender - g->top + g->height);
    return bottom;
  }
  std::vector<std::string> wrappedText(int, const char* text, int, int maxLines) const {
    std::vector<std::string> lines;
    std::string value(text);
    size_t start = 0, end;
    do {
      end = value.find('|', start);
      lines.push_back(value.substr(start, end - start));
      start = end + 1;
    } while (end != std::string::npos && static_cast<int>(lines.size()) < maxLines);
    return lines;
  }
  void drawText(int, int x, int y, const char* text) const {
    ys.push_back(y);
    while (*text) {
      const auto* g = glyph(next(text));
      if (!g) continue;
      for (int row = 0; row < g->height; ++row)
        for (int col = 0; col < g->width; ++col) {
          const int bit = row * g->width + col;
          if (font().bitmap[g->dataOffset + bit / 8] & (128 >> (bit % 8)))
            drawPixel(x + g->left + col, y + font().ascender - g->top + row, true);
        }
      x += g->advanceX / 16;
    }
  }
  void drawCenteredText(int font, int y, const char* text) const { drawText(font, 100, y, text); }
  void drawPixel(int x, int y, bool black) const {
    if (x >= 0 && x < 480 && y >= 0 && y < 800) pixels[y * 480 + x] = black;
  }
  void fillRect(int x, int y, int w, int h, bool black) const {
    for (int row = y; row < y + h; ++row)
      for (int col = x; col < x + w; ++col) drawPixel(col, row, black);
  }
  void fillRectDither(int, int, int, int, Color) const {}
  Orientation getOrientation() const { return Portrait; }
  void setOrientation(Orientation) const {}
  int getRenderMode() const { return BW; }
  bool grayPlanesAreAbsolute() const { return false; }
};
namespace tenorchrome {
inline bool enabled() { return true; }
int statusTextGrowth(bool large = false) {
  const int tier = SETTINGS.uiTextSize;
  return large ? (tier == 0 ? 0 : tier == 1 ? 5 : 10) : (tier == 0 ? 0 : tier == 1 ? 5 : 12);
}
int statusIconTopY(int height, bool large, int paddingBottom = 0) {
  return height - 24 - (large ? 10 : 0) - paddingBottom - statusTextGrowth(large) + 5;
}
struct StatusCornerBounds { int leftEnd, rightStart; };
StatusCornerBounds statusCornerBounds(const GfxRenderer&, bool) { return {65, 435}; }
void drawStatus(const GfxRenderer&) {}
// CHROME_DECLARATIONS
}
namespace buttonSymbols {
struct SymbolBounds { int left, right; };
SymbolBounds horizontalBounds(const char* label, int) { return std::strcmp(label, "symbol") == 0 ? SymbolBounds{8, 8} : SymbolBounds{0, 0}; }
bool drawLabel(const GfxRenderer&, const char* label, int, int, int) { return std::strcmp(label, "symbol") == 0; }
}
int fallbackCalls = 0;
struct BaseTheme {
  static void drawHintLabel(const GfxRenderer&, int, const char*, int, int, int, int, int);
};
struct TenorTheme {
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) const;
  static void drawHintLabel(const GfxRenderer& r, int font, const char* label, int x, int width, int top, int height, int offset) {
    ++fallbackCalls;
    assert(top == 800 - UITheme::getInstance().getMetrics().buttonHintsHeight);
    assert(height == UITheme::getInstance().getMetrics().buttonHintsHeight - 5);
    assert(offset == 4);
    BaseTheme::drawHintLabel(r, font, label, x, width, top, height, offset);
  }
};
// PRODUCTION_FUNCTIONS

int main() {
  int failures = 0;
  const auto check = [&](bool value, const char* message) {
    if (!value) { std::printf("FAIL tier=%d: %s\n", SETTINGS.uiTextSize, message); ++failures; }
  };
  for (int tier = 0; tier < 3; ++tier) {
    SETTINGS.uiTextSize = tier;
    for (const char* text : {"g", "Ậ", "Ấ Ắ Ễ Ổ Ợ g p q y", "Giữ nút để chọn chương", "Dòng Ấ|Dòng g"}) {
      GfxRenderer r;
      SETTINGS.large = false;
      const auto lines = r.wrappedText(1, text, 432, 4);
      tenorchrome::drawTip(r, text);
      const int bottom = r.ys.back() + r.getTextInkBottom(1, lines.back().c_str(), EpdFontFamily::REGULAR);
      const int iconTop = tenorchrome::statusIconTopY(800, false) - 1;
      check(iconTop - bottom == 2, "small tip must leave exactly 2px after real descender ink");
      int actualBottom = 0;
      for (int y = 0; y < 800; ++y)
        for (int x = 0; x < 480; ++x) if (r.pixels[y * 480 + x]) actualBottom = y + 1;
      check(actualBottom == bottom, "glyph bounds differ from actual last painted row");
      const auto before = r.pixels;
      TenorTheme{}.drawButtonHints(r, "symbol", "symbol", "symbol", "symbol");
      check(r.pixels == before, "footer clear erased a tip painted before the symbols");
      check(r.ys.size() == lines.size(), "wrapped tip line count changed");
      SETTINGS.large = true;
      GfxRenderer large;
      tenorchrome::drawTip(large, text);
      check(large.ys.back() == 800 - UITheme::getInstance().getMetrics().buttonHintsHeight - r.getLineHeight(1) - 5,
            "large icon tip anchor changed");
    }
    SETTINGS.large = false;
    GfxRenderer fallback;
    tenorchrome::drawTip(fallback, "g", 0, 4, true);
    const int legacy = 800 - UITheme::getInstance().getMetrics().buttonHintsHeight - fallback.getLineHeight(1) - 5;
    check(fallback.ys.back() == legacy, "explicit text fallback lost its two-line hint reserve");
    const auto before = fallback.pixels;
    TenorTheme{}.drawButtonHints(fallback, "Chọn|mạng", "", "", "");
    bool retained = true;
    for (size_t i = 0; i < before.size(); ++i) if (before[i] && !fallback.pixels[i]) retained = false;
    check(retained, "text fallback clear erased a legacy tip");
    check(fallback.ys.size() == 3, "fallback did not render its two supplied wrapped lines");
    SETTINGS.tenorButtonSymbols = false;
    GfxRenderer text;
    tenorchrome::drawTip(text, "g");
    check(text.ys.back() == legacy, "text mode tip anchor changed");
    SETTINGS.tenorButtonSymbols = true;
    SETTINGS.hidden = true;
    GfxRenderer off;
    tenorchrome::drawTip(off, "g");
    TenorTheme{}.drawButtonHints(off, "symbol", "symbol", "symbol", "symbol");
    check(off.ys.empty() && std::none_of(off.pixels.begin(), off.pixels.end(), [](auto p) { return p != 0; }), "status off drew footer ink");
    SETTINGS.hidden = false;
    std::printf("tier=%d caption=%d fallback reserve=%d\n", tier, text.getLineHeight(1), UITheme::getInstance().getMetrics().buttonHintsHeight);
  }
  assert(fallbackCalls == 3);
  std::printf("%s: actual glyph pixels, descenders, stacked accents, wrapped tips, paint order, large/text/off\n", failures ? "RED" : "GREEN");
  return failures ? 1 : 0;
}
