#pragma once
// Pure decisions of the tenor/ugly shell: integer only, no hardware, so a host test runs them as they are.
#include <algorithm>

#include "components/PageRows.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <Utf8.h>

#include "UglyLevel.h"
#include "activities/home/DocThuMuc.h"

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

// What turns one letter of the baked font at draw time: the same letter at the same size is always turned
// the same way. Integers only (the C3 has no FPU). Ranges as the baked font had them: up to 7 degrees,
// size 88% to 118%, up to 2 px of lift at size 22 (scaled with the size), advance 88% to 104% of the
// scaled one. sinQ12 and cosQ12 are in 1/4096.
struct Warp {
  int sinQ12, cosQ12;
  int scalePct, dy, stepPct;
};
inline constexpr Warp NO_WARP = {0, 4096, 100, 0, 100};

inline uint32_t warpHash(const uint32_t cp, const int px, const uint32_t salt) {
  uint32_t h = cp * 73856093u ^ static_cast<uint32_t>(px) * 19349663u ^ salt * 83492791u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  h *= 3266489917u;
  return h ^ (h >> 16);
}

inline Warp warpOf(const uint32_t cp, const int px) {
  Warp w;
  const int eighths = static_cast<int>(warpHash(cp, px, 1) % 113u) - 56;  // -7 to +7 degrees in 1/8 degree
  w.sinQ12 = eighths * 143 / 16;                                          // sin of a small angle, in 1/4096
  w.cosQ12 = 4096 - ((w.sinQ12 * w.sinQ12) >> 13);
  w.scalePct = 88 + static_cast<int>(warpHash(cp, px, 2) % 31u);
  w.dy = (static_cast<int>(warpHash(cp, px, 3) % 17u) - 8) * px / 88;
  w.stepPct = 88 + static_cast<int>(warpHash(cp, px, 4) % 17u);
  return w;
}

// Advance in pixels of a letter whose font advance is `advanceQ4` (1/16 px), after the warp.
inline int warpAdvance(const int advanceQ4, const Warp& w) {
  return (advanceQ4 * w.scalePct * w.stepPct / 10000 + 8) >> 4;
}

// A box in pixels from the pen (x right, y down from the baseline); x1 and y1 are exclusive.
struct WarpBox {
  int x0, y0, x1, y1;
};
inline constexpr WarpBox NO_CLIP = {-4096, -4096, 4096, 4096};

// Turns a straight glyph (1 bit per pixel, rows packed one after the other, `left` and `top` as the font gives
// them) about the middle of its advance on the baseline, scales it by w.scalePct and lifts it by w.dy, then
// calls plot(x, y) for every inked pixel of the result that falls inside `clip`, x and y counted from the pen on
// the baseline. Each destination pixel looks up the source pixel under its centre, so the work is the size of the
// box, not of the ink.
template <class Plot>
inline WarpBox warpGlyph(const uint8_t* bits, const int gw, const int gh, const int left, const int top, const int advanceQ4,
                         const Warp& w, const WarpBox& clip, Plot plot) {
  const int pivot = advanceQ4 * 8;  // half the advance in 1/256 px
  // Forward, to find the box: the four corners of the source.
  int minX = 1 << 30, minY = 1 << 30, maxX = -(1 << 30), maxY = -(1 << 30);
  for (int c = 0; c < 4; ++c) {
    const int dx = ((c & 1 ? left + gw : left) << 8) - pivot, dy = (c & 2 ? -top + gh : -top) << 8;
    const int fx = (w.cosQ12 * dx - w.sinQ12 * dy) / 4096 * w.scalePct / 100;
    const int fy = (w.sinQ12 * dx + w.cosQ12 * dy) / 4096 * w.scalePct / 100;
    minX = std::min(minX, pivot + fx);
    maxX = std::max(maxX, pivot + fx);
    minY = std::min(minY, (w.dy << 8) + fy);
    maxY = std::max(maxY, (w.dy << 8) + fy);
  }
  const WarpBox box = {std::max(minX >> 8, clip.x0), std::max(minY >> 8, clip.y0), std::min((maxX >> 8) + 1, clip.x1),
                       std::min((maxY >> 8) + 1, clip.y1)};
  // Backward, per pixel: the matrix that undoes the turn and the scale, in 1/4096.
  const int a = w.cosQ12 * 100 / w.scalePct, b = w.sinQ12 * 100 / w.scalePct;
  const int offU = (pivot << 12) - (left << 20), offV = top << 20;
  for (int y = box.y0; y < box.y1; ++y) {
    const int qy = (y << 8) + 128 - (w.dy << 8), qx0 = (box.x0 << 8) + 128 - pivot;
    int u = a * qx0 + b * qy + offU, v = a * qy - b * qx0 + offV;
    for (int x = box.x0; x < box.x1; ++x, u += a << 8, v -= b << 8) {
      const int i = u >> 20, j = v >> 20;
      if (static_cast<unsigned>(i) >= static_cast<unsigned>(gw) || static_cast<unsigned>(j) >= static_cast<unsigned>(gh)) continue;
      const int bit = j * gw + i;
      if ((bits[bit >> 3] >> (7 - (bit & 7))) & 1) plot(x, y);
    }
  }
  return box;
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

// Rows of a notebook page: the shared rule of paged rows (a lone last row stands in the foot line).
using pagerows::pageCount;
using pagerows::pageNamed;
using pagerows::pageOf;
using pagerows::pageTop;
using pagerows::rowsOnPage;

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

// The ugly Folder page keeps its own ceiling within the shared heap budget.
using docthumuc::FOLDER_BYTES_PER_ROW;
using docthumuc::FOLDER_HEAP_KEEP;
inline constexpr size_t FOLDER_MAX_ROWS = 2000;
inline size_t folderCap(const size_t freeHeap, const size_t largestBlock) {
  return std::min(FOLDER_MAX_ROWS, docthumuc::tran(freeHeap, largestBlock));
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
// centres sit within rowTol of each other vertically, a column the same horizontally. Up and Down go to the
// nearest row above or below and pick the centre nearest to fromX (the x where the last sideways step ended, so
// down and up again comes back); Left and Right stay in the row. There is no dead end: Down off the bottom goes
// to the top of the next column on the right (the last column wraps to the first), Up off the top goes to the
// bottom of the next column on the left (the first wraps to the last), Right off the end of a row goes to the
// first thing of the row below (the last row wraps to the first), Left off the start goes to the last thing of the
// row above. Returns `cur` only when there is nowhere else to go.
struct Point {
  int x, y;
};
enum class GridDir : uint8_t { Up, Down, Left, Right };
inline int gridStep(const Point* p, const int n, const int cur, const GridDir dir, const int fromX, const int rowTol) {
  // Among the things on the line at `at` (a row when !byX, a column when byX), the one furthest along the other
  // axis: the largest value when `last`, else the smallest.
  const auto pick = [&](const bool byX, const int at, const bool last) {
    int best = -1;
    for (int i = 0; i < n; ++i) {
      if (std::abs((byX ? p[i].x : p[i].y) - at) > rowTol) continue;
      const int across = byX ? p[i].y : p[i].x, bestAcross = best < 0 ? 0 : (byX ? p[best].y : p[best].x);
      if (best < 0 || (last ? across > bestAcross : across < bestAcross)) best = i;
    }
    return best;
  };
  // The line coordinate nearest to cur's beyond rowTol in a direction (sign +1 or -1); none left, the far end.
  const auto nextLine = [&](const bool byX, const int sign) {
    const int from = byX ? p[cur].x : p[cur].y;
    int near = 0, far = 0;
    bool haveNear = false, haveFar = false;
    for (int i = 0; i < n; ++i) {
      const int v = byX ? p[i].x : p[i].y;
      if ((v - from) * sign > rowTol && (!haveNear || (v - from) * sign < (near - from) * sign)) near = v, haveNear = true;
      if (!haveFar || (v - far) * sign < 0) far = v, haveFar = true;  // the end the other way round, for the wrap
    }
    return haveNear ? near : far;
  };
  const auto sameRow = [&](const int i) { return std::abs(p[i].y - p[cur].y) <= rowTol; };
  int best = cur;
  if (dir == GridDir::Left || dir == GridDir::Right) {
    const int sign = dir == GridDir::Right ? 1 : -1;
    for (int i = 0; i < n; ++i)
      if (i != cur && sameRow(i) && (p[i].x - p[cur].x) * sign > 0 &&
          (best == cur || (p[i].x - p[cur].x) * sign < (p[best].x - p[cur].x) * sign))
        best = i;
    if (best != cur) return best;
    // End of the row: first (Right) or last (Left) thing of the row below (above), wrapping round.
    const int row = nextLine(false, sign);
    const int hit = pick(false, row, dir == GridDir::Left);
    return hit < 0 ? cur : hit;
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
  if (found) return best;
  // Off the bottom (top): the top (bottom) of the next column to the right (left), wrapping round.
  const int col = nextLine(true, sign);
  const int hit = pick(true, col, dir == GridDir::Up);
  return hit < 0 ? cur : hit;
}

// ---- the lines of abuse (UglyQuip.h, scripts/ugly/gen_quips.py) ----
// The key of the Vietnamese words of an event: FNV-1a over them joined by '=', folded to 16 bits.
inline uint16_t quipKey(const char* words, const char* more = nullptr) {
  uint32_t h = 2166136261u;
  for (const char* part : {words, more}) {
    if (!part) continue;
    if (part == more) h = (h ^ '=') * 16777619u;
    for (const auto* p = reinterpret_cast<const unsigned char*>(part); *p; ++p) h = (h ^ *p) * 16777619u;
  }
  return static_cast<uint16_t>(h ^ (h >> 16));
}
// The slot of an event, its key and condition, else of the same event and key with no condition; -1 none.
// `Slot` is quips::Slot: key, what (event << 4 | condition), count.
template <class Slot>
int quipSlot(const Slot* slots, const int n, const int event, const uint16_t key, const int when) {
  int plain = -1;
  for (int i = 0; i < n; ++i) {
    if (slots[i].key != key || (slots[i].what >> 4) != event) continue;
    if ((slots[i].what & 15) == when) return i;
    if ((slots[i].what & 15) == 0) plain = i;
  }
  return plain;
}
// Which of `count` lines the shell says now, for every line it has (sleep, wake, abuse): they take turns by `turn`
// (a counter kept in RTC, so a wake does not start them over), and the line shown last never comes twice in a
// row. id(i) names line i the way `last` does, so a pool that changed since (another hour) still knows it.
// -1 when there is no line.
template <class Id>
int takeTurn(const uint32_t turn, const int count, const uint32_t last, Id id) {
  if (count <= 0) return -1;
  const int at = static_cast<int>(turn % static_cast<uint32_t>(count));
  return count > 1 && id(at) == last ? (at + 1) % count : at;
}

// A step on a cycle of `count` stops.
inline int cycle(const int index, const int step, const int count) {
  return count <= 0 ? 0 : ((index + step) % count + count) % count;
}

}  // namespace ugly::logic
