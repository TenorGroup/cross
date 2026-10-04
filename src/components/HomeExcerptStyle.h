#pragma once
#include <Utf8.h>

#include <cstddef>
#include <cstdint>

inline bool homeExcerptUsesUiFont(const char* text) {
  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  while (*cursor) {
    if (utf8IsCjkCodepoint(utf8NextCodepoint(&cursor))) return true;
  }
  return false;
}

// Which saved quote the Recent card shows for a book, as an index into `ids` (newest first, as
// quotes::listNames returns them; `count` must be at least one). `lastNewest` is the newest id the
// book had when Home last showed it, 0 when Home has not shown it since power-on. A newer one means
// a quote was kept since, most likely in the book the reader just left, so the card shows it.
// Otherwise any quote of the book, from `randomValue`, so the card does not repeat one line on
// every visit. Ids of one book share their high 32 bits; the low 32 bits (day, minute, slot)
// order them by the moment they were kept, which is why a deleted newest quote does not count.
// `marked` is the quote the store saved or edited last and no screen has shown yet
// (quotes::latestSaved), 0 when none: it wins when it is one of this book's, because after deep
// sleep `lastNewest` is 0 while the mark on the card still says what was kept.
inline size_t homeQuoteIndex(const uint64_t* ids, const size_t count, const uint64_t lastNewest,
                             const uint32_t randomValue, const uint64_t marked = 0) {
  for (size_t i = 0; marked != 0 && i < count; ++i)
    if (ids[i] == marked) return i;
  if (lastNewest != 0 && static_cast<uint32_t>(ids[0]) > static_cast<uint32_t>(lastNewest)) return 0;
  return randomValue % count;
}

// Cover size on the card at the default text size; larger text shrinks it in the same shape. The
// stats column to its right is a third of the 480 px text width (160 px).
constexpr int HOME_CARD_COVER_W = 298;
constexpr int HOME_CARD_COVER_H = 450;

// The Recent card, top to bottom: the cover at the left margin with the book's reading stats in a
// column to its right, then title (up to two lines), author and one line of excerpt from the
// cover's left edge across the width, then a rule and the other-book row just above the footer.
// The cover is sized for a one-line title and its one line of excerpt, and a two-line title takes
// that line's room: the excerpt goes first, the cover keeps its size. Line heights come from the
// flash fonts in use, so a larger text size shrinks the cover instead of pushing text off screen.
struct HomeCardInput {
  int screenWidth = 0;
  int top = 0;     // first row under the tab band
  int bottom = 0;  // last row the other-book row may use
  int titleLineHeight = 0;      // the line the cover is sized for
  int titleDrawLineHeight = 0;  // the line this book's title is drawn in when it differs, else 0
  int titleLines = 0;  // lines the title actually wraps to, 1 or 2
  int authorLineHeight = 0;
  int excerptLineHeight = 0;
  int rowLineHeight = 0;
};
struct HomeCardLayout {
  int coverX = 0, coverY = 0, coverW = 0, coverH = 0;
  int textX = 0, textW = 0;
  int titleY = 0, authorY = 0, excerptY = 0, excerptLines = 0;
  int ruleY = 0, rowY = 0;
  int statsX = 0, statsRight = 0;
};
inline HomeCardLayout homeCardLayout(const HomeCardInput& in) {
  constexpr int COVER_W = HOME_CARD_COVER_W, COVER_H = HOME_CARD_COVER_H, COVER_MIN_H = 120;
  constexpr int MARGIN = 24, STATS_GAP = 22, COVER_GAP = 18, AUTHOR_GAP = 2, EXCERPT_GAP = 7, RULE_GAP = 15, ROW_GAP = 8;
  constexpr int EXCERPT_LINES = 1;
  HomeCardLayout card;
  card.textX = MARGIN;
  card.textW = in.screenWidth - 2 * MARGIN;
  card.rowY = in.bottom - in.rowLineHeight;
  card.ruleY = card.rowY - ROW_GAP;
  // The cover is sized for a one-line title and one line of excerpt whatever this book's title is,
  // so it keeps one size while the reader steps between books. A second title line takes the
  // excerpt's room, so a two-line title shows no excerpt.
  const int text = COVER_GAP + in.titleLineHeight + AUTHOR_GAP + in.authorLineHeight + EXCERPT_GAP +
                   EXCERPT_LINES * in.excerptLineHeight + RULE_GAP;
  const int room = card.ruleY - in.top - text;
  card.coverH = room > COVER_H ? COVER_H : room < COVER_MIN_H ? COVER_MIN_H : room;
  card.coverW = card.coverH * COVER_W / COVER_H;
  card.coverX = MARGIN;
  card.coverY = in.top;
  card.statsX = card.coverX + card.coverW + STATS_GAP;
  card.statsRight = in.screenWidth - MARGIN;
  card.titleY = card.coverY + card.coverH + COVER_GAP;
  card.authorY =
      card.titleY + in.titleLines * (in.titleDrawLineHeight ? in.titleDrawLineHeight : in.titleLineHeight) + AUTHOR_GAP;
  card.excerptY = card.authorY + in.authorLineHeight + EXCERPT_GAP;
  const int lines = in.excerptLineHeight > 0 ? (card.ruleY - RULE_GAP - card.excerptY) / in.excerptLineHeight : 0;
  card.excerptLines = lines > EXCERPT_LINES ? EXCERPT_LINES : lines < 0 ? 0 : lines;
  return card;
}

// Rows of the reading stats column beside the cover, top to bottom.
enum HomeStat : uint8_t {
  HOME_STAT_READ,
  HOME_STAT_FINISH,
  HOME_STAT_TOTAL,
  HOME_STAT_AVERAGE,
  HOME_STAT_DAYS,
  HOME_STAT_SPAN,
  HOME_STAT_COUNT
};

// Which rows a book's reading record fills, one bit per HomeStat. The percent row always shows; a
// book with no record shows it alone (it then says the book was not recorded). Otherwise a row
// shows only when it has a number behind it: time for the total, reading days for the days and
// the average, a first and last day for the span, an estimate (`finish`, ngaydocxong::uocTinh gave
// one) for the expected finish. The average is the stats screen's "per reading day" figure, total
// time over reading days.
inline uint8_t homeStatRows(const bool recorded, const uint64_t elapsedMs, const uint32_t days,
                            const uint32_t firstDay, const uint32_t lastDay, const bool finish = false) {
  uint8_t rows = 1u << HOME_STAT_READ;
  if (!recorded) return rows;
  if (finish) rows |= 1u << HOME_STAT_FINISH;
  if (elapsedMs > 0) rows |= 1u << HOME_STAT_TOTAL;
  if (elapsedMs > 0 && days > 0) rows |= 1u << HOME_STAT_AVERAGE;
  if (days > 0) rows |= 1u << HOME_STAT_DAYS;
  if (firstDay != 0 && lastDay != 0) rows |= 1u << HOME_STAT_SPAN;
  return rows;
}

// The column: each row is a label line, then a value line, and the percent row adds a progress
// bar under its value. Absent rows close up. Rows are laid out within the cover's height, so the
// title below never moves; the first row that does not fit ends the column (larger text sizes).
struct HomeStatsInput {
  int top = 0, bottom = 0;
  int labelLineHeight = 0, valueLineHeight = 0;
  int valueTail = 0;  // value line height less its ascender: the empty rows under a value's baseline
  uint8_t rows = 0;
};
struct HomeStatsLayout {
  int labelY[HOME_STAT_COUNT] = {}, valueY[HOME_STAT_COUNT] = {};
  int barY = -1;  // top of the progress bar, -1 when the percent row is not drawn
  uint8_t rows = 0;  // the rows that fit
};
constexpr int HOME_STATS_BAR_H = 10;
// Between groups: from a value's baseline, or from the bottom of the progress bar, to the next label's
// line. The bar has no tail under it, so it is given the value's, and both gaps look the same.
inline HomeStatsLayout homeStatsLayout(const HomeStatsInput& in) {
  constexpr int TOP_PAD = 2, BAR_GAP = 2, ROW_GAP = 16;
  HomeStatsLayout column;
  int y = in.top + TOP_PAD;
  for (int row = 0; row < HOME_STAT_COUNT; ++row) {
    if (!(in.rows & (1u << row))) continue;
    const int valueY = y + in.labelLineHeight;
    int end = valueY + in.valueLineHeight;
    const int barY = end + BAR_GAP;
    if (row == HOME_STAT_READ) end = barY + HOME_STATS_BAR_H;
    if (end > in.bottom) break;
    column.labelY[row] = y;
    column.valueY[row] = valueY;
    if (row == HOME_STAT_READ) column.barY = barY;
    column.rows |= 1u << row;
    y = end + ROW_GAP + (row == HOME_STAT_READ ? in.valueTail : 0);
  }
  return column;
}
