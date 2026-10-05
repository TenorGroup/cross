#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "CrossPointSettings.h"
#include "components/UIScale.h"
struct EpdFontFamily { enum Style { REGULAR, BOLD }; };
enum Color : uint8_t { Clear=0x00, White=0x01, LightGray=0x05, DarkGray=0x0A, Black=0x10 };
class GfxRenderer {
 public:
  struct Shape { int x,y,w,h,radius,line; bool fill; };
  mutable std::vector<Shape> shapes;
  int width = 528, height = 792, subtitleExtra = 0;
  struct Run { int x, y, width, height; std::string text; };
  mutable std::vector<Run> runs, dots;
  struct Line { int x1,y1,x2,y2; };
  mutable std::vector<Line> lines;
  mutable std::vector<Shape> rectangles;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  int getLineHeight(int font) const {
    const auto spec = uiTextSizeSpec(SETTINGS.uiTextSize);
    return font == SMALL_FONT_ID ? spec.captionLineHeight : font == UI_10_FONT_ID ? spec.subtitleLineHeight + subtitleExtra :
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
    if (std::string(value) == ".") dots.push_back({x,y,getTextWidth(font,value,style),getLineHeight(font),value});
    if (*value && std::string(value) != ".") runs.push_back({x, y, getTextWidth(font, value, style), getLineHeight(font), value});
  }
  void drawRoundedRect(int x,int y,int w,int h,int line,int radius,bool) const { shapes.push_back({x,y,w,h,radius,line,false}); }
  void fillRoundedRect(int x,int y,int w,int h,int radius,Color) const { shapes.push_back({x,y,w,h,radius,0,true}); }
  void fillRect(int x,int y,int w,int h,bool = true) const { rectangles.push_back({x,y,w,h,0,0,true}); }
  void drawLine(int x1,int y1,int x2,int y2,bool = true) const { lines.push_back({x1,y1,x2,y2}); }
};
