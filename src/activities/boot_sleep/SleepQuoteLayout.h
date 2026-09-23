#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// Shape of the "Book + quotation" sleep screen (mockup S2): the big opening quote mark top
// left, the quote in Noto Serif filling the screen, and a bottom row with a small cover of
// the quote's own book beside its title and the chapter and page. Pure arithmetic and
// string handling, no renderer, so the choices below are checked on a desktop
// (test/sleep_quote) instead of by eye on the panel.
namespace sleepquote {

// Body column. The screen is 528 px wide; the right inset matches the mockup.
constexpr int16_t MARGIN_X = 48;
constexpr int16_t RIGHT_INSET = 44;
// The opening mark (components/QuoteMarkGlyph.h), placed where the mockup draws its ink.
constexpr int16_t GLYPH_X = 47;
constexpr int16_t GLYPH_Y = 82;
// Top of the quote's first line box.
constexpr int16_t BODY_TOP = 138;
// The bottom row: cover top, and the room the quote must leave above it.
constexpr int16_t ROW_TOP = 596;
constexpr int16_t BODY_ROW_GAP = 30;
constexpr int16_t COVER_W = 96;
constexpr int16_t COVER_H = 145;
// Cover to the title column, and the title's small drop against the cover's top edge.
constexpr int16_t COVER_TEXT_GAP = 20;
constexpr int16_t TITLE_DROP = 6;
constexpr int16_t PLACE_GAP = 6;
constexpr int TITLE_MAX_LINES = 3;

// Lines of `lineHeight` that fit between BODY_TOP and the gap above the bottom row.
inline int bodyLineLimit(const int lineHeight) {
  if (lineHeight <= 0) return 0;
  return (ROW_TOP - BODY_ROW_GAP - BODY_TOP) / lineHeight;
}

struct Fit {
  int size = 0;     // index into the caller's list of body sizes
  bool cut = false; // true when even the last size needs more lines than fit
};

// Tries the body sizes in order (Noto Serif 18, 16, 14) and keeps the first one the whole
// quote fits at. `lineCount(i, limit)` wraps the quote at size i with at most limit + 1
// lines and returns how many lines came back, so a count above `limit` means it does not
// fit; the caller stops wrapping as soon as a size fits. When none fits, the last size is
// kept and the quote is cut there.
template <typename LineCount>
Fit chooseFit(const int* lineHeights, const int sizes, LineCount&& lineCount) {
  for (int i = 0; i < sizes; i++) {
    const int limit = bodyLineLimit(lineHeights[i]);
    if (limit > 0 && lineCount(i, limit) <= limit) return {i, false};
  }
  return {sizes > 0 ? sizes - 1 : 0, true};
}

constexpr size_t NO_INDEX = static_cast<size_t>(-1);

// Index of the quote to show out of `count`, never `avoid` (the one shown last time) when
// there is any other. `draw` is one number from the platform generator; a single draw over
// the remaining count keeps every other quote equally likely without a retry loop.
inline size_t pickIndex(const size_t count, const size_t avoid, const uint32_t draw) {
  if (count == 0) return NO_INDEX;
  if (count == 1) return 0;
  if (avoid >= count) return draw % count;
  const size_t index = draw % (count - 1);
  return index >= avoid ? index + 1 : index;
}

// "Chương %d, trang %d" out of the list screen's place format "Chương %d, trang %d,
// %02u/%02u/%04u": everything before the date, less the separator in front of it (", " in
// Vietnamese and English, a full-width comma in Chinese). Empty when the format has no date
// field, so the caller falls back to the chapter alone instead of passing a format that
// asks for arguments it was not given.
inline std::string placeFormat(const char* listPlace) {
  if (!listPlace) return {};
  const char* date = std::strstr(listPlace, "%02u");
  if (!date) return {};
  std::string format(listPlace, static_cast<size_t>(date - listPlace));
  static constexpr char FULL_WIDTH_COMMA[] = "\xef\xbc\x8c";
  for (;;) {
    if (!format.empty() && (format.back() == ' ' || format.back() == ',')) {
      format.pop_back();
    } else if (format.size() >= 3 && format.compare(format.size() - 3, 3, FULL_WIDTH_COMMA) == 0) {
      format.erase(format.size() - 3);
    } else {
      break;
    }
  }
  return format;
}

// Last line of a quote that did not fit even at the smallest size. The wrap already cut it
// with an ellipsis, possibly in the middle of a word and with no room for the closing mark;
// this backs off to the last whole word, then ends the line with the ellipsis and the
// closing mark inside `maxWidth`. Text with no spaces (CJK) backs off one codepoint at a
// time instead.
template <typename Width>
std::string closeCutLine(std::string line, const int maxWidth, Width&& width) {
  static constexpr char ELLIPSIS[] = "\xe2\x80\xa6";
  static constexpr char ENDING[] = "\xe2\x80\xa6\xe2\x80\x9d";  // ellipsis, closing double quote
  static constexpr char CLOSING[] = "\xe2\x80\x9d";
  for (const char* tail : {CLOSING, ELLIPSIS}) {
    const size_t n = std::strlen(tail);
    if (line.size() >= n && line.compare(line.size() - n, n, tail) == 0) line.erase(line.size() - n);
  }
  const auto dropTail = [&line]() {
    const size_t space = line.rfind(' ');
    if (space != std::string::npos && space > 0) {
      line.erase(space);
    } else if (!line.empty()) {
      size_t cut = line.size() - 1;
      while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) cut--;
      line.erase(cut);
    }
  };
  // The wrap cut inside a word unless the character it cut before was a space.
  if (line.find(' ') != std::string::npos) dropTail();
  while (!line.empty() && (line.back() == ' ' || line.back() == ',')) line.pop_back();
  while (!line.empty() && width(line + ENDING) > maxWidth) {
    dropTail();
    while (!line.empty() && (line.back() == ' ' || line.back() == ',')) line.pop_back();
  }
  return line + ENDING;
}

// A title cut by the wrap right after a space or a comma ends "word, …"; the ellipsis
// belongs on the word. Lines that do not end in an ellipsis are left alone.
inline void tidyEllipsis(std::string& line) {
  static constexpr char ELLIPSIS[] = "\xe2\x80\xa6";
  constexpr size_t n = sizeof(ELLIPSIS) - 1;
  if (line.size() < n || line.compare(line.size() - n, n, ELLIPSIS) != 0) return;
  size_t end = line.size() - n;
  while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == ',')) end--;
  if (end > 0) line.erase(end, line.size() - n - end);
}

}  // namespace sleepquote
