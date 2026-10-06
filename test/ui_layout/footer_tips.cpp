#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <CjkTextWrap.h>
#include <Utf8.h>
#include <builtinFonts/geist_8_regular.h>
#include <builtinFonts/geist_10_regular.h>
#include <builtinFonts/geist_12_regular.h>

constexpr int SMALL_FONT_ID = 1;
namespace BidiUtils { enum class BidiBaseDir { AUTO }; }
struct Settings {
  int uiTextSize = 0;
  bool hidden = false, large = false, tenorButtonSymbols = true;
  bool globalStatusBarHidden() const { return hidden; }
  bool globalStatusBarLarge() const { return large; }
  int clockFormat = 0;
} SETTINGS;
// The hand-drawn key bar of tenor/ugly asks the clock; this harness measures tenor/cross only.
namespace clockstatus { inline bool hasValidTime() { return false; } }
struct { bool formatTime(char*, int, bool) const { return false; } } halClock;
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
  mutable std::vector<std::string> paintedText;
  mutable int attemptedInkTop = 800, attemptedInkBottom = 0;
  mutable int lineTop = 800, lineBottom = 0;
  int screenHeight = 800;
  int fontOverride = -1;
  const EpdFontData& font() const {
    const int face = fontOverride < 0 ? SETTINGS.uiTextSize : fontOverride;
    return face == 0 ? geist_8_regular : face == 1 ? geist_10_regular : geist_12_regular;
  }
  const EpdFont& fontObject() const {
    static const EpdFont fonts[] = {EpdFont(&geist_8_regular), EpdFont(&geist_10_regular), EpdFont(&geist_12_regular)};
    return fonts[SETTINGS.uiTextSize];
  }
  const std::map<int, EpdFontFamily> fontMap = {{SMALL_FONT_ID, EpdFontFamily(&fontObject())}};
  int resolveTextFontId(int fontId, const char*, EpdFontFamily::Style) const { return fontId; }
  const char* resolveVisualText(const char* text, std::string&, BidiUtils::BidiBaseDir) const { return text; }
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
  int getScreenHeight() const { return screenHeight; }
  int getScreenWidth() const { return 480; }
  int getLineHeight(int) const { return font().advanceY; }
  int getFontAscenderSize(int) const { return font().ascender; }
  int getTextHeight(int) const { return font().ascender; }
  int getTextWidth(int, const char* text, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    int width = 0, height = 0;
    EpdFont(&font()).getTextDimensions(text, &width, &height);
    return width;
  }
  std::string truncatedText(int fontId, const char* text, int maxWidth, EpdFontFamily::Style style) const {
    if (getTextWidth(fontId, text, style) <= maxWidth) return text;
    std::string item(text);
    while (!item.empty() && getTextWidth(fontId, (item + "…").c_str(), style) >= maxWidth) utf8RemoveLastChar(item);
    return item + "…";
  }
  int getTextInkTop(int, const char*, EpdFontFamily::Style) const;
  int getTextInkBottom(int, const char*, EpdFontFamily::Style) const;
  std::vector<std::string> wrappedTextProduction(int, const char*, int, int, EpdFontFamily::Style) const;
  std::vector<std::string> wrappedText(int fontId, const char* text, int width, int maxLines) const {
    if (!std::strchr(text, '|')) return wrappedTextProduction(fontId, text, width, maxLines, EpdFontFamily::REGULAR);
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
    paintedText.emplace_back(text);
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
    if (black) { attemptedInkTop = std::min(attemptedInkTop, y); attemptedInkBottom = std::max(attemptedInkBottom, y + 1); }
    if (x >= 0 && x < 480 && y >= 0 && y < 800) pixels[y * 480 + x] = black;
  }
  void fillRect(int x, int y, int w, int h, bool black) const {
    for (int row = y; row < y + h; ++row)
      for (int col = x; col < x + w; ++col) drawPixel(col, row, black);
  }
  void fillRectDither(int, int, int, int, Color) const {}
  void drawLine(int, int y, int, int endY) const {
    lineTop = std::min(lineTop, std::min(y, endY));
    lineBottom = std::max(lineBottom, std::max(y, endY) + 1);
  }
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
int labelId(const char*) { return -1; }
}
// tenor/cross geometry only: the hand-drawn branch of tenor/ugly is never taken here.
namespace shell {
inline bool uglyParts() { return false; }
}
namespace ugly {
enum class Size { S22 };
enum class Mark { Left, Right, Up, Down, Tick, Back };
inline int paragraph(const GfxRenderer&, Size, int, int, int, int, const char*, bool = true) { return 1; }
inline void mark(const GfxRenderer&, Mark, int, int) {}
inline std::string fit(const GfxRenderer&, Size, const std::string& s, int) { return s; }
inline int width(const GfxRenderer&, Size, const char*) { return 0; }
inline int ascent(Size) { return 20; }
inline int text(const GfxRenderer&, Size, int, int, const char*, bool = true) { return 0; }
}
int fallbackCalls = 0;
struct BaseTheme {
  static void drawHintLabel(const GfxRenderer&, int, const char*, int, int, int, int, int);
};
struct TenorTheme {
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) const;
  static void drawHintLabel(const GfxRenderer& r, int font, const char* label, int x, int width, int top, int height, int offset) {
    ++fallbackCalls;
    assert(top == r.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight);
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
      int measuredTop = 800;
      for (size_t i = 0; i < lines.size(); ++i)
        measuredTop = std::min(measuredTop, r.ys[i] + r.getTextInkTop(1, lines[i].c_str(), EpdFontFamily::REGULAR));
      // Offline ink tuning leaves one blank bitmap row on a few glyphs. The
      // glyph box is a conservative bound and must contain every painted row.
      check(measuredTop <= r.attemptedInkTop && r.attemptedInkTop - measuredTop <= 1,
            "ink-top API fails to bound the first painted glyph row");
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

    const char* firstForcedLine = "Ở mọi thẻ, giữ tick để ghim hoặc gỡ.";
    const char* secondForcedLine = "Trong Yêu thích, giữ tick để đổi vị trí.";
    const std::string forcedLines = std::string(firstForcedLine) + "\n" + secondForcedLine;
    GfxRenderer forced;
    tenorchrome::drawTip(forced, forcedLines.c_str());
    check(forced.paintedText == std::vector<std::string>{firstForcedLine, secondForcedLine},
          "literal newline must produce separate footer-tip lines");
    check(forced.ys.size() == 2 && forced.ys[1] - forced.ys[0] == forced.getLineHeight(1),
          "literal newline must preserve caption line spacing");
    check(tenorchrome::tipHeight(forced, forcedLines.c_str(), 4) == forced.getLineHeight(1) * 2 + 7,
          "literal newline must reserve both footer-tip lines");
    check(tenorchrome::tipTopY(forced, forcedLines.c_str()) ==
              forced.ys.front() + forced.getTextInkTop(1, firstForcedLine, EpdFontFamily::REGULAR),
          "footer-tip reserve must begin at the first actual glyph row");
  }
  assert(fallbackCalls == 3);
  for (int tier = 0; tier < 3; ++tier) {
    SETTINGS.uiTextSize = tier;
    for (const char* label : {"Cập nhật", "Chọn mạng", "Download", "Mạng", "Tải về", "ẤN CHỌN", "ẬP ẬP"}) {
      GfxRenderer r;
      r.screenHeight = 792;
      const int reserve = UITheme::getInstance().getMetrics().buttonHintsHeight;
      const int boxTop = r.getScreenHeight() - reserve;
      const int boxBottom = r.getScreenHeight() - 5;
      TenorTheme{}.drawButtonHints(r, "", "", "", label);
      std::printf("label=%s tier=%d width=%d lines=%zu ink=[%d,%d) box=[%d,%d)\n", label, tier,
                  r.getTextWidth(1, label), r.ys.size(), r.attemptedInkTop, r.attemptedInkBottom, boxTop, boxBottom);
      check(r.attemptedInkTop >= boxTop && r.attemptedInkBottom <= boxBottom,
            "fallback label ink escapes its box into panel edge or dither");
      // A two-word action wraps only when it is wider than one line holds: Geist 8 sets "Cập nhật" in 69 px.
      const bool wraps = r.getTextWidth(1, label) > 72;
      if (wraps && (std::strcmp(label, "Cập nhật") == 0 || std::strcmp(label, "Chọn mạng") == 0))
        check(r.ys.size() == 2, "readable two-word action lost its second line");
      if (wraps && std::strcmp(label, "Cập nhật") == 0)
        check(r.paintedText == std::vector<std::string>{"Cập", "nhật"}, "update action lost letters or accents");
      if (std::strcmp(label, "Chọn mạng") == 0)
        check(r.paintedText == std::vector<std::string>{"Chọn", "mạng"}, "network action lost letters or accents");
      if (r.getTextWidth(1, label) <= 72) {
        const int expected = boxTop + (tier == 0 ? 4 : std::max(1, (reserve - 5 - r.getLineHeight(1)) / 2));
        check(r.ys.front() == expected, "ordinary single-line label moved");
      }
    }
  }
  // Same production painter, with the 40px Base/Lyra and 35px Tenor boxes.
  // Small caption fonts also model Base's 10pt hint font while uiTextSize=0.
  for (int face = 0; face < 2; ++face) {
    for (int height : {35, 40}) {
      for (const char* label : {"Cập nhật", "Chọn mạng", "Download", "Tải xuống", "ẤN CHỌN"}) {
        SETTINGS.uiTextSize = face;
        GfxRenderer r;
        SETTINGS.uiTextSize = 0;
        // Renderer font methods use fontMap below; preserve the selected face
        // independently of the layout tier for this Base-theme comparison.
        r.fontOverride = face;
        BaseTheme::drawHintLabel(r, 1, label, 58, 80, 752, height, 4);
        check(r.attemptedInkTop >= 752 && r.attemptedInkBottom <= 752 + height,
              "small wrapped label escapes another theme's box");
        if (r.paintedText.size() == 1 && r.getTextWidth(1, label) > 72)
          check(r.paintedText[0].find("…") != std::string::npos, "too-tall label hid truncation");
      }
    }
  }
  std::printf("%s: actual glyph pixels, descenders, stacked accents, wrapped tips, paint order, large/text/off\n", failures ? "RED" : "GREEN");
  return failures ? 1 : 0;
}
