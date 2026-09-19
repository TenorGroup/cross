#pragma once
#include <cstring>
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
    invalidUtf8Seen = false;
  }
  int getScreenWidth() const { return 528; }
  int getScreenHeight() const { return 792; }
  int getLineHeight(int) const { return 20; }
  int getTextAdvanceX(int, const char* s, EpdFontFamily::Style) const { inspectUtf8(s); return std::strlen(s) * advancePerByte; }
  int getTextWidth(int, const char* s) const { inspectUtf8(s); return std::strlen(s) * advancePerByte; }
  void drawText(int, int, int, const char* s, bool = true) const { inspectUtf8(s); drawnText.emplace_back(s); }
  template <typename... T> void drawCenteredText(T...) const {}
  template <typename... T> void fillRect(T...) const {}
  template <typename... T> void drawLine(T...) const {}
  void displayBuffer() const {}
};
