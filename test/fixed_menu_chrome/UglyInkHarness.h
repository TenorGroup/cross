#include "shells/ugly/UglyLogic.h"
#include "shells/ugly/UglyTables.h"
#include "shells/ugly/fonts/ugly_22.h"
namespace ugly {
enum class Size { S22 };
enum class Mark { Left, Right, Up, Down, Tick, Back };
enum class Circle { Word, Object, Row };
inline bool traceStatus = false;
struct TextRun { int left, baseline; std::string value; };
inline std::vector<TextRun> statusText;
inline std::vector<std::array<int, 3>> statusMarks;
struct Box { int x0, y0, x1, y1; };
inline int ascent(Size) { return 20; }
inline int width(const GfxRenderer&, Size, const char* value) {
  int span = 0;
  const EpdFont font(&ugly_22);
  while (*value) {
    const auto* glyph = font.getGlyph(GfxRenderer::next(value));
    if (glyph) span += fp4::toPixel(glyph->advanceX);
  }
  return span;
}
inline int text(const GfxRenderer& renderer, Size size, int left, int baseline, const char* value) {
  if (traceStatus) statusText.push_back({left, baseline, value});
  renderer.paintedText.emplace_back(value);
  renderer.ys.push_back(baseline);
  const int span = width(renderer, size, value);
  const EpdFont font(&ugly_22);
  while (*value) {
    const auto* glyph = font.getGlyph(GfxRenderer::next(value));
    if (!glyph) continue;
    for (int row = 0; row < glyph->height; ++row)
      for (int column = 0; column < glyph->width; ++column) {
        const int bit = row * glyph->width + column;
        if (font.data->bitmap[glyph->dataOffset + bit / 8] & (128 >> (bit % 8)))
          renderer.drawPixel(left + glyph->left + column, baseline - glyph->top + row, true);
      }
    left += fp4::toPixel(glyph->advanceX);
  }
  return span;
}
inline int paragraph(const GfxRenderer& renderer, Size size, int left, int baseline, int room, int step,
                     const char* value, bool draw = true) {
  std::vector<std::string> words;
  for (const char* start = value; *start;) {
    const char* end = std::strchr(start, ' ');
    words.emplace_back(start, end ? end - start : std::strlen(start));
    if (!end) break;
    start = end + 1;
  }
  std::vector<logic::Token> tokens;
  for (const auto& word : words) tokens.push_back({word.c_str(), 0, false});
  std::vector<logic::Placed> placed(tokens.size());
  const int lines = logic::layout(tokens.data(), static_cast<int>(tokens.size()), room,
                                  width(renderer, size, "a") / 2 + 4,
                                  [&](const char* word) { return width(renderer, size, word); }, placed.data());
  if (draw)
    for (size_t index = 0; index < tokens.size(); ++index)
      text(renderer, size, left + placed[index].x, baseline + placed[index].line * step, tokens[index].text);
  return lines;
}
void mark(const GfxRenderer& renderer, Mark mark, int left, int top);
inline std::string fit(const GfxRenderer&, Size, const std::string& value, int) { return value; }
}
