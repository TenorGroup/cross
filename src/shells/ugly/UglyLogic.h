#pragma once
// Pure decisions of the tenor/ugly shell: integer only, no hardware, so a host test runs them as they are.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <Utf8.h>

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

// Where to cut a line so that it ends in "..." and fits `maxWidth`, in one pass over the characters.
// adv(pos, cp) is the advance of the character at index pos, jump step included. Returns -1 when the
// whole line fits, -2 when nothing fits (not even the dots alone), else the bytes of the text to keep:
// the longest piece, shorter than the whole, that still fits with its dots.
template <class Advance>
int ellipsisKeep(const char* text, const int maxWidth, Advance adv) {
  const auto* start = reinterpret_cast<const unsigned char*>(text);
  const auto* p = start;
  int cursor = 0, pos = 0, keep = -2;
  while (*p) {
    if (cursor + adv(pos, '.') + adv(pos + 1, '.') + adv(pos + 2, '.') <= maxWidth) keep = static_cast<int>(p - start);
    cursor += adv(pos, utf8NextCodepoint(&p));
    ++pos;
  }
  return cursor <= maxWidth ? -1 : keep;
}

// How many names of the card root the Folder page may hold, from the heap left. Each row costs a string
// slot (the vector doubles, so twice that must fit in one block) and its name on the heap, and some heap
// stays for the frame and the card library. 0 means the page refuses the root rather than run out.
inline constexpr size_t FOLDER_MAX_ROWS = 2000, FOLDER_HEAP_KEEP = 16 * 1024, FOLDER_BYTES_PER_ROW = 80;
inline size_t folderCap(const size_t freeHeap, const size_t largestBlock) {
  const size_t byFree = freeHeap > FOLDER_HEAP_KEEP ? (freeHeap - FOLDER_HEAP_KEEP) / FOLDER_BYTES_PER_ROW : 0;
  const size_t byBlock = largestBlock / (2 * sizeof(std::string));
  return std::min({FOLDER_MAX_ROWS, byFree, byBlock});
}

// A title as it is compared: composed, letters lowercased, blanks and marks of punctuation dropped, so
// a heading that differs from the table of contents only in case, in a colon or in a double space is
// still the same title. Vietnamese capitals fold; a script without case is left as it is.
inline uint32_t foldLetter(const uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + 32;
  if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;
  if (cp == 0x1AF) return 0x1B0;
  const bool even = (cp >= 0x100 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177) || cp == 0x1A0 ||
                    (cp >= 0x1EA0 && cp <= 0x1EFF);
  return even && (cp & 1) == 0 ? cp + 1 : cp;
}

inline std::string foldTitle(const std::string& utf8) {
  const std::string composed = utf8ComposeNfc(utf8);
  std::string out;
  const auto* p = reinterpret_cast<const unsigned char*>(composed.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    const bool mark = cp < 0x80 ? !((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
                                : (cp == 0xA0 || (cp >= 0x2000 && cp <= 0x206F) || cp == 0x3000);
    if (!mark) utf8AppendCodepoint(foldLetter(cp), out);
  }
  return out;
}

// How many of the first lines of a page are the chapter's own heading: the fewest lines, at most
// `count`, whose text joined is the folded `title`. 0 when the page does not open with it.
inline int headingLines(const std::string* lines, const int count, const std::string& foldedTitle) {
  if (foldedTitle.empty()) return 0;
  std::string joined;
  for (int i = 0; i < count; ++i) {
    joined += foldTitle(lines[i]);
    if (joined.size() >= foldedTitle.size()) return joined == foldedTitle ? i + 1 : 0;
  }
  return 0;
}

// Walking a picture by where things are, as the on-screen keyboard walks its grid. A row is the things whose
// centres sit within rowTol of each other vertically. Up and Down go to the nearest row above or below and pick
// the centre nearest to fromX (the x where the last sideways step ended, so down and up again comes back);
// Left and Right stay in the row and stop at its edge. Returns `cur` when there is nowhere to go.
struct Point {
  int x, y;
};
enum class GridDir : uint8_t { Up, Down, Left, Right };
inline int gridStep(const Point* p, const int n, const int cur, const GridDir dir, const int fromX, const int rowTol) {
  const auto sameRow = [&](const int i) { return std::abs(p[i].y - p[cur].y) <= rowTol; };
  int best = cur;
  if (dir == GridDir::Left || dir == GridDir::Right) {
    const int sign = dir == GridDir::Right ? 1 : -1;
    for (int i = 0; i < n; ++i)
      if (i != cur && sameRow(i) && (p[i].x - p[cur].x) * sign > 0 &&
          (best == cur || (p[i].x - p[cur].x) * sign < (p[best].x - p[cur].x) * sign))
        best = i;
    return best;
  }
  const int sign = dir == GridDir::Down ? 1 : -1;
  int rowY = 0;
  bool found = false;
  for (int i = 0; i < n; ++i)  // the nearest row in that direction
    if ((p[i].y - p[cur].y) * sign > rowTol && (!found || (p[i].y - p[cur].y) * sign < (rowY - p[cur].y) * sign)) {
      rowY = p[i].y;
      found = true;
    }
  for (int i = 0; i < n && found; ++i)
    if (std::abs(p[i].y - rowY) <= rowTol && (best == cur || std::abs(p[i].x - fromX) < std::abs(p[best].x - fromX))) best = i;
  return best;
}

// A step on a cycle of `count` stops.
inline int cycle(const int index, const int step, const int count) {
  return count <= 0 ? 0 : ((index + step) % count + count) % count;
}

}  // namespace ugly::logic
