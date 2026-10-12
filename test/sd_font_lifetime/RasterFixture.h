#pragma once
#include RUBY_BLOCK_HEADER
#include <GfxRenderer.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

unsigned long rasterBlockCalls = 0, rasterAdvanceCalls = 0;
namespace {
unsigned width = 792, height = 528;
std::vector<uint8_t> pixels;
struct Face {
  std::vector<uint8_t> bitmap;
  std::vector<EpdGlyph> glyphs;
  std::vector<EpdUnicodeInterval> intervals;
  EpdFontData data{};
  EpdFont font;
  Face(int tier, int variant) : font(&data) {
    std::vector<uint32_t> cps;
    for (uint32_t cp = 32; cp < 127; ++cp) cps.push_back(cp);
    for (uint32_t cp : {0x0301u, 0x0302u, 0x0303u, 0x0323u, 0x1ea5u, 0x1ec5u, 0x1ec7u,
                       0x4e00u, 0x5b57u, 0x6f22u, 0xfffdu}) cps.push_back(cp);
    for (uint32_t cp : cps) {
      const uint8_t w = 6 + tier + variant + (cp == 'j' ? 11 : 0), h = 9 + tier * 2 + (cp == 'j' ? 9 : 0);
      const uint32_t offset = bitmap.size();
      const uint16_t count = (w * h + 3) / 4;
      bitmap.resize(offset + count);
      for (int n = 0; n < w * h; ++n)
        bitmap[offset + n / 4] |= ((n + cp + variant) % 4) << (6 - (n % 4) * 2);
      intervals.push_back({cp, cp, static_cast<uint32_t>(glyphs.size())});
      glyphs.push_back({w, h, static_cast<uint16_t>((w + 1) * 16), static_cast<int16_t>(-variant - (cp == 'j' ? 12 : 0)),
                        static_cast<int16_t>(h + (cp < 0x400 && cp >= 0x300 ? 7 : 0)), count, offset});
    }
    data.bitmap = bitmap.data(); data.glyph = glyphs.data(); data.intervals = intervals.data();
    data.intervalCount = intervals.size(); data.advanceY = 18 + tier * 3;
    data.ascender = 14 + tier * 2; data.descender = -4; data.is2Bit = true;
  }
};
void drawCorpus(GfxRenderer& renderer, int y) {
  using Style = EpdFontFamily::Style;
  TextBlock one({"ấ", "漢字", "ễ", "ệ"}, {0, 70, 160, 210},
                {EpdFontFamily::REGULAR, EpdFontFamily::ITALIC, EpdFontFamily::BOLD, EpdFontFamily::SUB},
                {}, {}, BlockStyle{}, {"a", "reading", "", "last"});
  one.render(renderer, 42, 35, y);
  TextBlock group({"ấ", "ễ", "ệ", "tail"}, {0, 55, 120, 185},
    {static_cast<Style>(EpdFontFamily::UNDERLINE | EpdFontFamily::ITALIC),
     static_cast<Style>(EpdFontFamily::RUBY_CONTINUE | EpdFontFamily::UNDERLINE),
     static_cast<Style>(EpdFontFamily::RUBY_CONTINUE | EpdFontFamily::STRIKETHROUGH), EpdFontFamily::SUP},
    {}, {}, BlockStyle{}, {"a long annotation", "", "", "sup"});
  group.setLetterSpacing(2);
  group.render(renderer, 42, 30, y + 35);
  TextBlock cap({"Initial", "end"}, {0, 180}, {static_cast<Style>(EpdFontFamily::ITALIC | EpdFontFamily::DROP_CAP), EpdFontFamily::REGULAR},
                {}, {}, BlockStyle{}, {"drop", ""});
  cap.setDropCapHeight(38);
  cap.render(renderer, 42, 25, y + 80);
}
}
HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return width; }
uint16_t HalDisplay::getDisplayHeight() const { return height; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return width / 8; }
uint32_t HalDisplay::getBufferSize() const { return width / 8 * height; }
