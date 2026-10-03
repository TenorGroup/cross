#pragma once
// Pure decisions of the tenor/ugly shell: integer only, no hardware, so a host test runs them as they are.
#include <cstddef>
#include <cstdint>

namespace ugly::logic {

// The X3 portrait frame the baked pictures fill.
inline constexpr int FRAME_W = 528, FRAME_H = 792;
inline constexpr size_t FRAME_BYTES = FRAME_W * FRAME_H / 8;

// How a character jumps off the line, by its position in the string and its code point. Integers only
// (the C3 has no FPU), so the same string always lands on the same pixels.
inline int jumpSelector(const int pos, const uint32_t cp) { return static_cast<int>((static_cast<uint32_t>(pos) * 7u + cp) % 5u); }

// Rise or drop in pixels for a glyph of nominal size `px`: up to 2 px at size 38, scaled with the size.
inline int jumpDy(const int pos, const uint32_t cp, const int px) {
  static constexpr int8_t DY[5] = {-2, 1, 0, 2, -1};
  const int d = DY[jumpSelector(pos, cp)] * px;
  return (d + (d < 0 ? -19 : 19)) / 38;
}

// Extra step (px) added to the advance, so the gaps between letters are not even.
inline int jumpStep(const int pos, const uint32_t cp) {
  static constexpr int8_t STEP[5] = {0, 1, -1, 0, 1};
  return STEP[jumpSelector(pos, cp)];
}

// Hash offset in [-range, range] for the i-th point of a stroke drawn with `seed`.
inline int wobble(const uint32_t seed, const int i, const int range) {
  uint32_t h = seed * 2654435761u + static_cast<uint32_t>(i) * 40503u + 12345u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return static_cast<int>(h % static_cast<uint32_t>(2 * range + 1)) - range;
}

// Days since 1970-01-01 of a date coded as yyyymmdd (the code the reading statistics use).
inline int civilDays(const uint32_t code) {
  int y = static_cast<int>(code / 10000);
  const int m = static_cast<int>((code / 100) % 100);
  const int d = static_cast<int>(code % 100);
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

inline int daysBetween(const uint32_t earlier, const uint32_t later) { return civilDays(later) - civilDays(earlier); }

// Which of `count` sleep lines a day picks; the same day always picks the same one. A day the clock
// cannot name (0) picks the first.
inline int sleepLine(const uint32_t dayCode, const int count) {
  return dayCode == 0 || count <= 0 ? 0 : static_cast<int>(static_cast<uint32_t>(civilDays(dayCode)) % static_cast<uint32_t>(count));
}

// Which sentence opens the diary: by whether a book exists, the last day anything was read (0 for
// never) and today (0 when the clock does not know).
enum class DiaryKind : uint8_t { NoBook, NoStats, Today, Yesterday, Ago, Before };
inline constexpr int AGO_LIMIT_DAYS = 60;  // past this the count of days is no longer worth saying
inline DiaryKind diaryKind(const bool hasBook, const uint32_t lastDay, const uint32_t today, int* days = nullptr) {
  if (!hasBook) return DiaryKind::NoBook;
  if (lastDay == 0) return DiaryKind::NoStats;
  if (today == 0) return DiaryKind::Before;
  const int d = daysBetween(lastDay, today);
  if (days) *days = d;
  if (d <= 0) return DiaryKind::Today;
  if (d == 1) return DiaryKind::Yesterday;
  return d <= AGO_LIMIT_DAYS ? DiaryKind::Ago : DiaryKind::Before;
}

// One unit of a paragraph. text == nullptr ends the line. id > 0 is a word the diary underlines and
// the buttons walk. attach glues the token to the one before it (no space).
struct Token {
  const char* text;
  int8_t id;
  bool attach;
};
struct Placed {
  int line;
  int x;
  int w;
};

// Greedy line breaking of tokens that never split. Returns the number of lines. `measure(text)` gives
// the width of one token, `space` the width of a blank.
template <class Measure>
int layout(const Token* tokens, const int count, const int maxWidth, const int space, Measure measure, Placed* out) {
  int line = 0, x = 0;
  for (int i = 0; i < count; ++i) {
    if (!tokens[i].text) {
      ++line;
      x = 0;
      out[i] = {line, 0, 0};
      continue;
    }
    const int w = measure(tokens[i].text);
    int gap = (x == 0 || tokens[i].attach) ? 0 : space;
    if (x > 0 && !tokens[i].attach && x + gap + w > maxWidth) {
      ++line;
      x = 0;
      gap = 0;
    }
    out[i] = {line, x + gap, w};
    x += gap + w;
  }
  return line + 1;
}

// Rows of a notebook page: the page a selection falls on, and the first row of that page.
inline int pageOf(const int selected, const int rows) { return rows > 0 ? selected / rows : 0; }
inline int pageTop(const int selected, const int rows) { return pageOf(selected, rows) * rows; }
inline int pageCount(const int count, const int rows) { return rows > 0 && count > 0 ? (count + rows - 1) / rows : 1; }

// A step on a cycle of `count` stops.
inline int cycle(const int index, const int step, const int count) {
  return count <= 0 ? 0 : ((index + step) % count + count) % count;
}

}  // namespace ugly::logic
