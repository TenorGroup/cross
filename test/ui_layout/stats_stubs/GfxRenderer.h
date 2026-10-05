#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "CrossPointSettings.h"
#include "components/UIScale.h"
struct EpdFontFamily { enum Style { REGULAR, BOLD }; };
enum Color : uint8_t { Clear = 0x00, White = 0x01, LightGray = 0x05, DarkGray = 0x0A, Black = 0x10 };
class GfxRenderer {
 public:
  int width = 528, height = 792;
  struct Run { int x, y, width, height; std::string text; };
  mutable std::vector<Run> runs;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  int getLineHeight(int font) const {
    const auto spec = uiTextSizeSpec(SETTINGS.uiTextSize);
    return font == SMALL_FONT_ID ? spec.captionLineHeight : font == UI_10_FONT_ID ? spec.subtitleLineHeight :
           font == NOTOSANS_18_FONT_ID ? 51 : spec.bodyLineHeight;
  }
  int getTextWidth(int font, const char* value, EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    int chars = 0;
    for (auto p = reinterpret_cast<const unsigned char*>(value); *p; ++p) if ((*p & 0xc0) != 0x80) ++chars;
    return chars * getLineHeight(font) / 2;
  }
  std::string truncatedText(int font, const char* value, int maxWidth, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    std::string out(value);
    while (!out.empty() && getTextWidth(font, out.c_str(), style) > maxWidth) {
      do { out.pop_back(); } while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xc0) == 0x80);
      if (!out.empty() && static_cast<unsigned char>(out.back()) >= 0xc0) out.pop_back();
    }
    return out;
  }
  void drawText(int font, int x, int y, const char* value, bool = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    if (*value && std::string(value) != ".") runs.push_back({x, y, getTextWidth(font, value, style), getLineHeight(font), value});
  }
  void drawRoundedRect(int, int, int, int, int, int, bool) const {}
  void fillRoundedRect(int, int, int, int, int, Color) const {}
  template<class... Args> void fillRect(Args...) const {}
  template<class... Args> void drawLine(Args...) const {}
};
