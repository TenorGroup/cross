#include <cassert>
#include <cstdio>
#include "components/HomeExcerptStyle.h"
#include <builtinFonts/geist_12_regular.h>
#include <builtinFonts/geist_14_regular.h>
#include <builtinFonts/geist_16_regular.h>
#include <builtinFonts/notoserif_12_italic.h>
#include <builtinFonts/notoserif_14_italic.h>
#include <builtinFonts/notoserif_16_italic.h>

bool contains(const EpdFontData& font, uint32_t cp) {
  for (uint32_t i = 0; i < font.intervalCount; ++i)
    if (font.intervals[i].first <= cp && cp <= font.intervals[i].last) return true;
  return false;
}

// Quote ids as quotes::listNames returns them: book key in the high 32 bits, then day code,
// minute and slot, newest first.
constexpr uint64_t BOOK = 0x1caa51ccull << 32;
constexpr uint64_t id(uint32_t day, uint32_t minute, uint32_t slot) { return BOOK | day << 16 | minute << 4 | slot; }

void quotePick() {
  const uint64_t ids[] = {id(2090, 0, 3), id(2090, 0, 2), id(2090, 0, 1), id(2090, 0, 0)};
  // Not shown since power-on: any quote of the book, from the random value.
  assert(homeQuoteIndex(ids, 4, 0, 0) == 0);
  assert(homeQuoteIndex(ids, 4, 0, 6) == 2);
  assert(homeQuoteIndex(ids, 4, 0, 0xFFFFFFFFu) == 3);
  // Home showed the book when this was its newest quote and nothing was kept since: random.
  assert(homeQuoteIndex(ids, 4, ids[0], 5) == 1);
  assert(homeQuoteIndex(ids, 4, ids[0], 7) == 3);
  // A quote was kept since Home last showed the book: the newest, whatever the random value.
  const uint64_t later[] = {id(2091, 1241, 0), id(2090, 0, 3), id(2090, 0, 2)};
  assert(homeQuoteIndex(later, 3, ids[0], 2) == 0);
  // The same minute, a later slot, counts as later too.
  const uint64_t slot[] = {id(2090, 0, 4), id(2090, 0, 3)};
  assert(homeQuoteIndex(slot, 2, ids[0], 1) == 0);
  // The newest quote was deleted: the book's newest is now older than the one remembered. That is
  // no new quote, so the card picks at random again.
  const uint64_t fewer[] = {id(2090, 0, 2), id(2090, 0, 1)};
  assert(homeQuoteIndex(fewer, 2, ids[0], 1) == 1);
  // One quote: always that one.
  assert(homeQuoteIndex(ids + 3, 1, 0, 12345) == 0);
  puts("PASS: the card shows a quote kept since the last visit, otherwise a random one of the book");
}

// X3 at the default text size: Geist 12 title (33), Be Vietnam Pro 10 author and bottom row (26),
// Noto Serif 12 italic excerpt (34). Top is under the tab row, bottom is the top of the hint band.
HomeCardInput x3(int titleLines) {
  HomeCardInput in;
  in.screenWidth = 528;
  in.top = 124;
  in.bottom = 752;
  in.titleLineHeight = 33;
  in.titleLines = titleLines;
  in.authorLineHeight = 26;
  in.excerptLineHeight = 34;
  in.rowLineHeight = 26;
  return in;
}

void cardGeometry() {
  // The approved drawing: cover 236 x 356 centred under the tab row, title, author, three lines of
  // excerpt, then the rule and the other-book row above the footer.
  const auto card = homeCardLayout(x3(2));
  assert(card.coverW == 236 && card.coverH == 356);
  assert(card.coverX == (528 - 236) / 2 && card.coverY == 124);
  assert(card.textX == 40 && card.textW == 448);
  assert(card.titleY == 124 + 356 + 18);
  assert(card.authorY == card.titleY + 2 * 33 + 2);
  assert(card.excerptY == card.authorY + 26 + 8);
  assert(card.excerptLines == 3);
  assert(card.rowY == 752 - 26);
  assert(card.ruleY == card.rowY - 8);
  assert(card.excerptY + 3 * 34 + 16 <= card.ruleY);

  // A one-line title moves the author and excerpt up; the cover keeps its size, so switching
  // books does not make the cover jump.
  const auto shortTitle = homeCardLayout(x3(1));
  assert(shortTitle.coverH == 356 && shortTitle.coverY == 124);
  assert(shortTitle.authorY == shortTitle.titleY + 33 + 2);
  assert(shortTitle.excerptLines == 3);

  // Larger text: Geist 16 title (43) and Noto Serif 16 excerpt (45). The cover gives up height,
  // keeps its 236:356 shape and stays centred; the text still ends above the rule.
  auto large = x3(2);
  large.titleLineHeight = 43;
  large.excerptLineHeight = 45;
  const auto big = homeCardLayout(large);
  assert(big.coverH < 356 && big.coverH >= 200);
  assert(big.coverW == big.coverH * 236 / 356);
  assert(big.coverX == (528 - big.coverW) / 2);
  assert(big.excerptLines == 3);
  assert(big.excerptY + 3 * 45 + 16 <= big.ruleY);

  // A short screen: the cover stops at its floor and the excerpt drops lines instead, never below
  // one line and never past the rule.
  auto shortScreen = x3(2);
  shortScreen.bottom = 480;
  const auto small = homeCardLayout(shortScreen);
  assert(small.coverH == 120);
  assert(small.excerptLines >= 1 && small.excerptLines < 3);
  assert(small.excerptY + small.excerptLines * 34 <= small.ruleY);
  puts("PASS: card geometry matches the approved drawing and gives way to larger text");
}

int main() {
  const EpdFontData* serif[] = {&notoserif_12_italic, &notoserif_14_italic, &notoserif_16_italic};
  const EpdFontData* ui[] = {&geist_12_regular, &geist_14_regular, &geist_16_regular};
  const char* quote = "阅读时间和最近读过的书";
  assert(!homeExcerptUsesUiFont("Những ngày bình yên. Quiet reading."));
  for (int tier = 0; tier < 3; ++tier) {
#ifdef LEGACY_EXCERPT
    const auto* selected = serif[tier];
#else
    const auto* selected = homeExcerptUsesUiFont(quote) ? ui[tier] : serif[tier];
#endif
    const auto* p = reinterpret_cast<const unsigned char*>(quote);
    while (*p) {
      const auto cp = utf8NextCodepoint(&p);
      assert(contains(*selected, cp));
    }
  }
  puts("PASS: CJK excerpt resolves actual generated glyph intervals in all three tiers; Latin keeps serif");
  quotePick();
  cardGeometry();
}
