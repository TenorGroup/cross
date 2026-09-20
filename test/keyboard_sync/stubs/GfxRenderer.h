#pragma once
#include <cstring>
#include "components/UIScale.h"
#include <functional>
#include <string>
#include <vector>

struct EpdFontFamily {
  enum Style { REGULAR, BOLD };
};

// The production keyboard owns layout, editing and all cursor spans. The
// boundary renderer only records text and can stop at the start of a frame.
class GfxRenderer {
 public:
  std::function<void()> frameStarted;
  mutable std::vector<std::string> drawnText;
  mutable bool invalidUtf8Seen = false;
  int advancePerByte = 8;
  uint8_t uiTier = 0;
  struct TextRun { int font, x, y; std::string text; };
  mutable std::vector<TextRun> runs;
  void inspectUtf8(const char* text) const {
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p) {
      const unsigned char first = *p++;
      if (first < 0x80) continue;
      const int count = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2 :
                        first >= 0xf0 && first <= 0xf4 ? 3 : -1;
      if (count < 0) { invalidUtf8Seen = true; return; }
      for (int i = 0; i < count; ++i) {
        if (p[i] < 0x80 || p[i] > 0xbf) { invalidUtf8Seen = true; return; }
        if (i == 0 && ((first == 0xe0 && p[i] < 0xa0) || (first == 0xed && p[i] >= 0xa0) ||
                       (first == 0xf0 && p[i] < 0x90) || (first == 0xf4 && p[i] >= 0x90))) {
          invalidUtf8Seen = true; return;
        }
      }
      p += count;
    }
  }
  void clearScreen() const {
    if (frameStarted) frameStarted();
    drawnText.clear();
    runs.clear();
    invalidUtf8Seen = false;
  }
  void getOrientedViewableTRBL(int* top, int* right, int* bottom, int* left) const {
    *top = 9; *right = 7; *bottom = 7; *left = 7;
  }
  int getScreenWidth() const { return 528; }
  int getScreenHeight() const { return 792; }
  int getLineHeight(int font) const { return uiTier == 0 ? 20 : font == SMALL_FONT_ID ? uiTextSizeSpec(uiTier).captionLineHeight : uiTextSizeSpec(uiTier).bodyLineHeight; }
  int getTextAdvanceX(int, const char* s, EpdFontFamily::Style) const { inspectUtf8(s); return std::strlen(s) * advancePerByte; }
  int getTextWidth(int, const char* s) const { inspectUtf8(s); return std::strlen(s) * advancePerByte; }
  std::string truncatedText(int, const char* text, int width) const {
    std::string out(text);
    if (static_cast<int>(out.size()) * advancePerByte > width) out.resize(std::max(0, width / advancePerByte));
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xc0) == 0x80) out.pop_back();
    return out;
  }
  void drawText(int font, int x, int y, const char* s, bool = true) const {
    inspectUtf8(s); drawnText.emplace_back(s); runs.push_back({font, x, y, s});
  }
  template <typename... T> void drawCenteredText(T...) const {}
  template <typename... T> void fillRect(T...) const {}
  template <typename... T> void drawLine(T...) const {}
  void displayBuffer() const {}
};
