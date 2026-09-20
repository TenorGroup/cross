#pragma once
#include <FreeInkUI.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "components/UIThemeSizing.h"
#include "components/themes/TenorTheme.h"
#include "components/themes/roundedraff/RoundedRaffTheme.h"
namespace fui = freeink::ui;
struct Target : fui::DrawTarget {
  UITextSizeSpec spec;
  std::vector<fui::Rect> runs;
  explicit Target(uint8_t tier) : spec(uiTextSizeSpec(tier)) {}
  fui::Size measureText(fui::FontId font, const char* text, fui::TextStyle) const override {
    int chars = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
      if ((*p & 0xc0) != 0x80) ++chars;
    return {static_cast<int16_t>(chars * lineHeight(font) / 2), lineHeight(font)};
  }
  int16_t lineHeight(fui::FontId font) const override { return font == 0 ? spec.subtitleLineHeight : spec.bodyLineHeight; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
  void text(fui::Rect rect, const char* text, fui::TextStyle style) override {
    fui::layoutText(*this, rect, text, style, [&](const char*, fui::Rect run) { runs.push_back(run); });
  }
};
