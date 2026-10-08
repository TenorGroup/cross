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
// cover's left edge across the width, then a rule and the other-book row (up to two lines) above the footer.
// The cover is sized for a one-line title and its one line of excerpt, and a two-line title takes
// that line's room: the excerpt goes first, the cover keeps its size. Line heights come from the
// flash fonts in use, so a larger text size shrinks the cover instead of pushing text off screen.
struct HomeCardInput {
  int screenWidth = 0;
  int statsMinWidth = 0;  // board/tier representative labels/values, including breathing room
  bool metadataAboveCover = false;
  int minStatsHeight = 0;  // fixed board/tier budget, including long duration rows
  int top = 0;     // first row under the tab band
  int bottom = 0;  // last row the other-book row may use
  int titleLineHeight = 0;      // the line the cover is sized for
  int titleDrawLineHeight = 0;  // the line this book's title is drawn in when it differs, else 0
  int titleLines = 0;  // lines the title actually wraps to, 1 or 2
  int authorLineHeight = 0;
  int excerptLineHeight = 0;
  int rowLineHeight = 0;
  int rowLines = 1;
};
struct HomeCardLayout {
  int coverX = 0, coverY = 0, coverW = 0, coverH = 0;
  int textX = 0, textW = 0;
  int titleY = 0, authorY = 0, excerptY = 0, excerptLines = 0;
  int ruleY = 0, rowY = 0;
  int statsX = 0, statsRight = 0, statsTop = 0;
};
inline HomeCardLayout homeCardLayout(const HomeCardInput& in) {
  constexpr int COVER_W = HOME_CARD_COVER_W, COVER_H = HOME_CARD_COVER_H, COVER_MIN_H = 120;
  constexpr int MARGIN = 24, STATS_GAP = 22, COVER_GAP = 18, AUTHOR_GAP = 2, EXCERPT_GAP = 7, RULE_GAP = 15, ROW_GAP = 8;
  constexpr int EXCERPT_LINES = 1;
  HomeCardLayout card;
  card.textX = MARGIN;
  card.textW = in.screenWidth - 2 * MARGIN;
  card.rowY = in.bottom - in.rowLineHeight * in.rowLines;
  card.ruleY = card.rowY - ROW_GAP;
  // A second other-book line takes excerpt room, keeping the cover's size independent of its title.
  const int oneLineRuleY = in.bottom - in.rowLineHeight - ROW_GAP;
  // The cover is sized for a one-line title and one line of excerpt whatever this book's title is,
  // so it keeps one size while the reader steps between books. A second title line takes the
  // excerpt's room, so a two-line title shows no excerpt.
  const int text = COVER_GAP + in.titleLineHeight + AUTHOR_GAP + in.authorLineHeight + EXCERPT_GAP +
                   EXCERPT_LINES * in.excerptLineHeight + RULE_GAP;
  const int room = oneLineRuleY - in.top - text;
  card.coverH = room > COVER_H ? COVER_H : room < COVER_MIN_H ? COVER_MIN_H : room;
  card.coverW = card.coverH * COVER_W / COVER_H;
  const int widthLimit = card.textW - STATS_GAP - in.statsMinWidth;
  if (in.statsMinWidth > 0 && card.coverW > widthLimit) {
    card.coverW = widthLimit;
    card.coverH = card.coverW * COVER_H / COVER_W;
  }
  if (in.metadataAboveCover) {
    const int metadata = 2 * in.titleLineHeight + AUTHOR_GAP + in.authorLineHeight + COVER_GAP;
    const int available = oneLineRuleY - RULE_GAP - in.top - metadata;
    if (card.coverH > available) {
      card.coverH = available;
      card.coverW = card.coverH * COVER_W / COVER_H;
    }
  }
  card.coverX = MARGIN;
  card.coverY = in.metadataAboveCover ? card.ruleY - RULE_GAP - card.coverH
                                    : in.top + (in.minStatsHeight > card.coverH ? in.minStatsHeight - card.coverH : 0);
  card.statsTop = in.top;
  card.statsX = card.coverX + card.coverW + STATS_GAP;
  card.statsRight = in.screenWidth - MARGIN;
  if (in.metadataAboveCover) card.textW = card.coverW;
  card.titleY = in.metadataAboveCover ? in.top : card.coverY + card.coverH + COVER_GAP;
  card.authorY =
      card.titleY + in.titleLines * (in.titleDrawLineHeight ? in.titleDrawLineHeight : in.titleLineHeight) + AUTHOR_GAP;
  card.excerptY = card.authorY + in.authorLineHeight + EXCERPT_GAP;
  const int excerptBottom = in.metadataAboveCover ? card.coverY - EXCERPT_GAP : card.ruleY - RULE_GAP;
  const int lines = in.excerptLineHeight > 0 ? (excerptBottom - card.excerptY) / in.excerptLineHeight : 0;
  card.excerptLines = in.metadataAboveCover && in.titleLines > 1 ? 0 :
                      lines > EXCERPT_LINES ? EXCERPT_LINES : lines < 0 ? 0 : lines;
  return card;
}

// Rows of the reading stats column beside the cover, top to bottom.
enum HomeStat : uint8_t {
  HOME_STAT_READ,
  HOME_STAT_FINISH,
  HOME_STAT_TOTAL,
  HOME_STAT_AVERAGE,
  HOME_STAT_DAYS,
  HOME_STAT_TURNS,
  HOME_STAT_COUNT
};

// Which rows a book's reading record fills, one bit per HomeStat. The percent row always shows; a
// book with no record shows it alone (it then says the book was not recorded). Otherwise a row
// shows only when it has a number behind it: time for the total, reading days for the days and
// the average, page turns for the turns row, an estimate (`finish`, ngaydocxong::uocTinh gave
// one) for the expected finish. The average is the stats screen's "per reading day" figure, total
// time over reading days.
inline uint8_t homeStatRows(const bool recorded, const uint64_t elapsedMs, const uint32_t days,
                            const uint32_t turns, const bool finish = false) {
  uint8_t rows = 1u << HOME_STAT_READ;
  if (!recorded) return rows;
  if (finish) rows |= 1u << HOME_STAT_FINISH;
  if (elapsedMs > 0) rows |= 1u << HOME_STAT_TOTAL;
  if (elapsedMs > 0 && days > 0) rows |= 1u << HOME_STAT_AVERAGE;
  if (days > 0) rows |= 1u << HOME_STAT_DAYS;
  if (turns > 0) rows |= 1u << HOME_STAT_TURNS;
  return rows;
}

// The column: each row is a label line, then a value line, and the percent row adds a progress
// bar under its value. Absent rows close up. Rows are laid out within the cover's height, with gaps shared across all present rows; enlarged cards put metadata above the cover.
struct HomeStatsInput {
  int top = 0, bottom = 0;
  int labelLineHeight = 0, valueLineHeight = 0;
  int valueTail = 0;  // value line height less its ascender: the empty rows under a value's baseline
  uint8_t rows = 0;
  int labelHeights[HOME_STAT_COUNT] = {};  // 0 uses labelLineHeight
  int valueHeights[HOME_STAT_COUNT] = {};  // 0 uses valueLineHeight, wrapped values reserve both lines
  int valueTails[HOME_STAT_COUNT] = {};
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
  constexpr int TOP_PAD = 2, BAR_GAP = 2;
  HomeStatsLayout column;
  int count = 0, linesHeight = 0, lastTail = in.valueTail;
  for (int row = 0; row < HOME_STAT_COUNT; ++row)
    if (in.rows & (1u << row)) {
      ++count;
      linesHeight += (in.labelHeights[row] ? in.labelHeights[row] : in.labelLineHeight) +
                     (in.valueHeights[row] ? in.valueHeights[row] : in.valueLineHeight);
      lastTail = in.valueHeights[row] ? in.valueTails[row] : in.valueTail;
    }
  if (!count) return column;
  const bool progress = in.rows & (1u << HOME_STAT_READ);
  const int used = linesHeight +
                   (progress ? BAR_GAP + HOME_STATS_BAR_H + in.valueTail : 0) - lastTail;
  const int spare = in.bottom - in.top - TOP_PAD - used;
  int y = in.top + TOP_PAD;
  int index = 0;
  for (int row = 0; row < HOME_STAT_COUNT; ++row) {
    if (!(in.rows & (1u << row))) continue;
    column.labelY[row] = y;
    column.valueY[row] = y + (in.labelHeights[row] ? in.labelHeights[row] : in.labelLineHeight);
    int end = column.valueY[row] + (in.valueHeights[row] ? in.valueHeights[row] : in.valueLineHeight);
    if (row == HOME_STAT_READ) {
      column.barY = end + BAR_GAP;
      end = column.barY + HOME_STATS_BAR_H + in.valueTail;
    }
    column.rows |= 1u << row;
    // Remainder pixels are shared between gaps, keeping the last value's ink edge on the cover edge.
    const int gap = count > 1 && spare > 0 ? spare * (index + 1) / (count - 1) - spare * index / (count - 1) : 0;
    y = end + gap;
    ++index;
  }
  return column;
}
