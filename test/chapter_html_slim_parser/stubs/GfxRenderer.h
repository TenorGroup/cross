#pragma once

#include <DropCap.h>
#include <EpdFontFamily.h>

#include <deque>
#include <string>

namespace BidiUtils {
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1 };
}

class GfxRenderer {
 public:
  int getDropCapAdvance(int, const char* text, EpdFontFamily::Style, int height, int = 0) const {
    return dropcap::initial(text).codepoint ? height / 2 + 4 : 0;
  }
  int getDropCapWordWidth(int f, const char* text, EpdFontFamily::Style s, int h, int = 0) const {
    auto c = dropcap::initial(text);
    return c.codepoint ? getDropCapAdvance(f, text, s, h) + getTextAdvanceX(f, text + c.endBytes, s)
                       : getTextAdvanceX(f, text, s);
  }
  void drawDropCapWord(int, int, int, const char*, EpdFontFamily::Style, int, int = 0) const {}
  bool isFontCacheScanning() const { return false; }
  void drawLine(int, int, int, int, int, bool) const {}
  void drawText(int, int, int, const char*, bool, EpdFontFamily::Style,
                BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO, int = 0, uint8_t = 0) const {}
  int getTextWidth(int font, const char* text, EpdFontFamily::Style style,
                   BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO, int spacing = 0) const {
    return getTextAdvanceX(font, text, style, spacing);
  }
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  int getLineHeight(int, float compression = 1.0f) const { return static_cast<int>(16 * compression + 0.5f); }
  int getFontAscenderSize(int) const { return 12; }
  // Muc mot dong = 12 + 6 = 18 px, cao hon o dong 16 px: giong font that (Bookerly 16: o 44, muc 45).
  int getFontDescenderSize(int) const { return 6; }
  // wordSpacing is a readerSpacing::Level; the stub keeps a fixed gap so the
  // parser tests measure line breaks, not the space delta.
  int getSpaceWidth(int, EpdFontFamily::Style, uint8_t = 0) const { return 4; }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style, int spacing = 0, uint8_t = 0) const {
    int width = 0;
    while (*text++) {
      if (width) width += spacing;
      width += 8;
    }
    return width;
  }
  int getKerning(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 0; }
  int getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style, uint8_t = 0) const { return 4; }
  bool isSdCardFont(int) const { return false; }
  void ensureSdCardFontReady(int, const std::deque<std::string>&, bool, uint8_t) const {}
};
