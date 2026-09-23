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
inline size_t homeQuoteIndex(const uint64_t* ids, const size_t count, const uint64_t lastNewest,
                             const uint32_t randomValue) {
  if (lastNewest != 0 && static_cast<uint32_t>(ids[0]) > static_cast<uint32_t>(lastNewest)) return 0;
  return randomValue % count;
}

// Cover size on the card at the default text size; larger text shrinks it in the same shape.
constexpr int HOME_CARD_COVER_W = 236;
constexpr int HOME_CARD_COVER_H = 356;

// The Recent card, top to bottom: cover, title (up to two lines), author, up to three lines of
// excerpt, then a rule and the other-book row just above the footer. Line heights come from the
// flash fonts in use, so a larger text size shrinks the cover instead of pushing text off screen.
struct HomeCardInput {
  int screenWidth = 0;
  int top = 0;     // first row under the tab band
  int bottom = 0;  // last row the other-book row may use
  int titleLineHeight = 0;
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
};
inline HomeCardLayout homeCardLayout(const HomeCardInput& in) {
  constexpr int COVER_W = HOME_CARD_COVER_W, COVER_H = HOME_CARD_COVER_H, COVER_MIN_H = 120;
  constexpr int MARGIN = 40, COVER_GAP = 18, AUTHOR_GAP = 2, EXCERPT_GAP = 8, RULE_GAP = 16, ROW_GAP = 8;
  constexpr int TITLE_LINES = 2, EXCERPT_LINES = 3;
  HomeCardLayout card;
  card.textX = MARGIN;
  card.textW = in.screenWidth - 2 * MARGIN;
  card.rowY = in.bottom - in.rowLineHeight;
  card.ruleY = card.rowY - ROW_GAP;
  // The cover is sized for a two-line title whatever this book's title is, so it keeps one size
  // while the reader steps between books.
  const int text = COVER_GAP + TITLE_LINES * in.titleLineHeight + AUTHOR_GAP + in.authorLineHeight + EXCERPT_GAP +
                   EXCERPT_LINES * in.excerptLineHeight + RULE_GAP;
  const int room = card.ruleY - in.top - text;
  card.coverH = room > COVER_H ? COVER_H : room < COVER_MIN_H ? COVER_MIN_H : room;
  card.coverW = card.coverH * COVER_W / COVER_H;
  card.coverX = (in.screenWidth - card.coverW) / 2;
  card.coverY = in.top;
  card.titleY = card.coverY + card.coverH + COVER_GAP;
  card.authorY = card.titleY + in.titleLines * in.titleLineHeight + AUTHOR_GAP;
  card.excerptY = card.authorY + in.authorLineHeight + EXCERPT_GAP;
  const int lines = in.excerptLineHeight > 0 ? (card.ruleY - RULE_GAP - card.excerptY) / in.excerptLineHeight : 0;
  card.excerptLines = lines > EXCERPT_LINES ? EXCERPT_LINES : lines < 1 ? 1 : lines;
  return card;
}
