#include <cassert>
#include <cstdio>
#include <algorithm>
#include <string>
#include <EpdFont.h>
#include "components/CompactStats.h"
#include <builtinFonts/geist_8_regular.h>
#include <builtinFonts/geist_10_regular.h>
#include <builtinFonts/geist_10_bold.h>
#include <builtinFonts/geist_12_bold.h>
#include <builtinFonts/geist_14_bold.h>
#include <builtinFonts/geist_16_bold.h>
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

// The quote the store marked as kept last and no screen has shown yet (quotes::latestSaved) wins
// over both rules above: after deep sleep Home has no memory of the book, but the card does.
void markedQuotePick() {
  const uint64_t ids[] = {id(2090, 0, 3), id(2090, 0, 2), id(2090, 0, 1), id(2090, 0, 0)};
  // Not shown since power-on, whatever the random value says.
  assert(homeQuoteIndex(ids, 4, 0, 0, ids[2]) == 2);
  assert(homeQuoteIndex(ids, 4, 0, 7, ids[0]) == 0);
  // An edit of an older quote is marked too, even when Home already saw the book's newest.
  assert(homeQuoteIndex(ids, 4, ids[0], 1, ids[3]) == 3);
  // A mark of another book, or of a quote since deleted, is not this card's business.
  const uint64_t other = (0x2ab0e838ull << 32) | id(2090, 0, 3);
  assert(homeQuoteIndex(ids, 4, 0, 6, other) == 2);
  assert(homeQuoteIndex(ids, 4, 0, 6, id(2089, 0, 0)) == 2);
  puts("PASS: a marked quote not shown yet is the one on the card");
}

// X3 at the default text size: Geist 12 title (33), Geist 10 author and bottom row (27),
// Noto Serif 12 italic excerpt (34). Top is under the tab row, bottom is the top of the hint band.
HomeCardInput x3(int titleLines) {
  HomeCardInput in;
  in.screenWidth = 528;
  in.top = 124;
  in.bottom = 752;
  in.titleLineHeight = 33;
  in.titleLines = titleLines;
  in.authorLineHeight = 27;
  in.excerptLineHeight = 34;
  in.rowLineHeight = 27;
  return in;
}

void cardGeometry() {
  // Plan F (04/10): cover 298 x 450 at the left margin under the tab row, the reading stats in a 160 px
  // column to its right (a third of the 480 px text width), then title, author and one line of excerpt at
  // the cover's left edge across the full width, and the rule and the other-book row above the footer.
  // The cover is sized for a one-line title and its excerpt line; a two-line title drops the excerpt.
  const auto card = homeCardLayout(x3(2));
  assert(card.coverW == 298 && card.coverH == 450);
  assert(card.coverX == 24 && card.coverY == 124);
  assert(card.statsX == 24 + 298 + 22 && card.statsRight == 528 - 24);
  assert(card.statsRight - card.statsX == 160);
  assert(card.textX == 24 && card.textW == 480);
  assert(card.titleY == 124 + 450 + 18);
  assert(card.authorY == card.titleY + 2 * 33 + 2);
  assert(card.excerptLines == 0);
  assert(card.rowY == 752 - 27);
  assert(card.ruleY == card.rowY - 8);
  assert(card.authorY + 27 + 15 <= card.ruleY);

  // A one-line title moves the author up and leaves room for one line of excerpt; the cover keeps its
  // size, so switching books does not make the cover jump.
  const auto shortTitle = homeCardLayout(x3(1));
  assert(shortTitle.coverH == 450 && shortTitle.coverY == 124);
  assert(shortTitle.authorY == shortTitle.titleY + 33 + 2);
  assert(shortTitle.excerptLines == 1);
  assert(shortTitle.excerptY == shortTitle.authorY + 27 + 7);
  assert(shortTitle.excerptY + 34 + 15 <= shortTitle.ruleY);

  // Larger text: Geist 16 title (43) and Noto Serif 16 excerpt (45). The cover gives up height,
  // keeps its 298:450 shape and its left edge, and the stats column takes the width it gives up.
  auto large = x3(2);
  large.titleLineHeight = 43;
  large.excerptLineHeight = 45;
  const auto big = homeCardLayout(large);
  assert(big.coverH < 450 && big.coverH >= 200);
  assert(big.coverW == big.coverH * 298 / 450);
  assert(big.coverX == 24);
  assert(big.statsX == 24 + big.coverW + 22 && big.statsRight == 504);
  assert(big.excerptLines == 0);
  // A one-line title keeps its excerpt line at the larger size, on the same cover.
  auto largeShort = large;
  largeShort.titleLines = 1;
  const auto bigShort = homeCardLayout(largeShort);
  assert(bigShort.coverH == big.coverH && bigShort.excerptLines == 1);
  assert(bigShort.excerptY + 45 + 15 <= bigShort.ruleY);

  // A short screen: the cover stops at its floor and the excerpt is dropped, never drawn past the rule.
  auto shortScreen = x3(2);
  shortScreen.bottom = 330;
  const auto small = homeCardLayout(shortScreen);
  assert(small.coverH == 120);
  assert(small.excerptLines == 0);
  puts("PASS: card geometry matches the approved drawing and gives way to larger text");
}

constexpr uint8_t bit(HomeStat row) { return static_cast<uint8_t>(1u << row); }
constexpr uint8_t ALL_STATS = bit(HOME_STAT_READ) | bit(HOME_STAT_TOTAL) | bit(HOME_STAT_AVERAGE) |
                              bit(HOME_STAT_DAYS) | bit(HOME_STAT_TURNS);

void statRows() {
  // A book read 7 h 25 min over 6 days, 05/08 to 17/08. Every row.
  const uint64_t hours = (7 * 60 + 25) * 60000ull;
  assert(homeStatRows(true, hours, 6, 412) == ALL_STATS);
  // No reading record: the percent row alone, which then says it was not recorded.
  assert(homeStatRows(false, hours, 6, 412) == bit(HOME_STAT_READ));
  // Opened but not read yet: nothing to total, average or date.
  assert(homeStatRows(true, 0, 0, 0) == bit(HOME_STAT_READ));
  // Time kept while the clock could not be read: a total, but no day to average over or to date.
  assert(homeStatRows(true, hours, 0, 0) == (bit(HOME_STAT_READ) | bit(HOME_STAT_TOTAL)));
  // Page turns only, on one day: days and dates, but no time to total or average.
  assert(homeStatRows(true, 0, 1, 3) ==
         (bit(HOME_STAT_READ) | bit(HOME_STAT_DAYS) | bit(HOME_STAT_TURNS)));
  puts("PASS: the stats column shows only the rows the book's record fills");
}

void statsColumn() {
  // X3 default: Geist 10 labels (27), Geist 12 bold values (33, 8 rows under the baseline), beside a
  // 450 px cover.
  const auto card = homeCardLayout(x3(2));
  HomeStatsInput in;
  in.top = card.coverY;
  in.bottom = card.coverY + card.coverH;
  in.labelLineHeight = 27;
  in.valueLineHeight = 33;
  in.valueTail = 8;
  in.rows = ALL_STATS;
  const auto column = homeStatsLayout(in);
  assert(column.rows == ALL_STATS);
  assert(column.labelY[HOME_STAT_READ] == card.coverY + 2);
  assert(column.valueY[HOME_STAT_READ] == column.labelY[HOME_STAT_READ] + 27);
  // The progress bar sits under the percent, then the next label.
  assert(HOME_STATS_BAR_H >= 8);
  assert(column.barY >= column.valueY[HOME_STAT_READ] + 33 - 6);
  assert(column.labelY[HOME_STAT_TOTAL] >= column.barY + HOME_STATS_BAR_H + 6);
  for (int row = HOME_STAT_TOTAL; row < HOME_STAT_COUNT; ++row) {
    assert(column.labelY[row] > column.valueY[row - 1]);
    assert(column.valueY[row] == column.labelY[row] + 27);
  }
  // The gap under the bar equals the gap between the groups below it, measured to the same line: from a
  // value's baseline (its line bottom less the tail), and from the bar's bottom, to the next label.
  assert(column.labelY[HOME_STAT_TOTAL] - (column.barY + HOME_STATS_BAR_H) ==
         column.labelY[HOME_STAT_AVERAGE] - (column.valueY[HOME_STAT_TOTAL] + 33 - in.valueTail));
  // The last value ends inside the cover's height, so the title below is not pushed down.
  assert(column.valueY[HOME_STAT_TURNS] + 33 - in.valueTail == in.bottom);

  // Sparse records retain their rows and distribute the available gap to the same bottom edge.
  in.rows = bit(HOME_STAT_READ) | bit(HOME_STAT_DAYS);
  const auto sparse = homeStatsLayout(in);
  assert(sparse.rows == in.rows);
  assert(sparse.valueY[HOME_STAT_DAYS] + 33 - in.valueTail == in.bottom);

  // Enlarged cards give the stats column the full body height instead of dropping rows.
  in.rows = ALL_STATS;
  in.bottom = in.top + 509;
  in.labelLineHeight = 33;
  in.valueLineHeight = 38;
  in.valueTail = 8;
  const auto enlarged = homeStatsLayout(in);
  assert(enlarged.rows == in.rows);
  assert(enlarged.valueY[HOME_STAT_TURNS] + 38 - 8 == in.bottom);
  puts("PASS: the stats column fits beside the cover and gives way to larger text");
}

void finishRow() {
  // v1.0.14: the expected finish date is the second row, under the percent and its bar, when the
  // record gives one (ngaydocxong::uocTinh); too little reading leaves it out.
  static_assert(HOME_STAT_FINISH == HOME_STAT_READ + 1);
  const uint64_t hours = (7 * 60 + 25) * 60000ull;
  assert(homeStatRows(true, hours, 6, 412, true) == (ALL_STATS | bit(HOME_STAT_FINISH)));
  assert(homeStatRows(true, hours, 6, 412, false) == ALL_STATS);
  assert(homeStatRows(false, hours, 6, 412, true) == bit(HOME_STAT_READ));
  // X3 default size: all six groups fit by distributing the available gap.
  const auto card = homeCardLayout(x3(2));
  HomeStatsInput in;
  in.top = card.coverY;
  in.bottom = card.coverY + card.coverH;
  in.labelLineHeight = 27;
  in.valueLineHeight = 33;
  in.valueTail = 8;
  in.rows = ALL_STATS | bit(HOME_STAT_FINISH);
  const auto column = homeStatsLayout(in);
  assert(column.rows == in.rows);
  assert(column.valueY[HOME_STAT_TURNS] + 33 - in.valueTail == in.bottom);
  assert(column.labelY[HOME_STAT_FINISH] >= column.barY + HOME_STATS_BAR_H + 6);
  assert(column.labelY[HOME_STAT_TOTAL] > column.valueY[HOME_STAT_FINISH]);
  puts("PASS: six reading-stat groups fit and the last value aligns to the cover bottom");
}

// Actual builtin glyphs and the renderer's differential rounding, including trailing spaces.
struct TestFaces {
  const EpdFontData* regular;
  const EpdFontData* bold;
};
int runPixels(const TestFaces& faces, const std::string& text, bool number) {
  EpdFont font(number ? faces.bold : faces.regular);
  int ink, height;
  font.getTextDimensions(text.c_str(), &ink, &height);
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  uint32_t previous = 0;
  int advance = 0;
  const EpdGlyph* glyph = nullptr;
  while (const auto cp = utf8NextCodepoint(&p)) {
    if (previous) advance += fp4::toPixel(glyph->advanceX + font.getKerning(previous, cp));
    glyph = font.getGlyph(cp);
    assert(glyph);
    previous = cp;
  }
  if (glyph) advance += fp4::toPixel(glyph->advanceX);
  return std::max(ink, advance);
}
int valuePixels(const TestFaces& faces, const std::string& text) {
  int width = 0;
  compactstats::runs(text.c_str(), [&](const std::string& part, bool number) {
    width += runPixels(faces, part, number);
  });
  return width;
}
int inkBottom(const TestFaces& faces, const std::string& text) {
  int bottom = 0;
  compactstats::runs(text.c_str(), [&](const std::string& part, bool number) {
    EpdFont font(number ? faces.bold : faces.regular);
    const auto* p = reinterpret_cast<const unsigned char*>(part.c_str());
    while (const auto cp = utf8NextCodepoint(&p)) {
      const auto* g = font.getGlyph(cp);
      assert(g);
      if (g->width && g->height)
        bottom = std::max(bottom, faces.regular->ascender - g->top + g->height);
    }
  });
  return bottom;
}

void measuredSixRows() {
  const TestFaces faces[] = {{&geist_8_regular, &geist_8_regular}, {&geist_10_regular, &geist_10_bold},
                            {&geist_12_regular, &geist_12_bold}, {&geist_14_regular, &geist_14_bold},
                            {&geist_16_regular, &geist_16_bold}};
  const char* labels[] = {"Đã đọc", "Dự kiến xong", "Tổng thời gian", "Trung bình", "Số ngày đọc", "Lượt lật trang"};
  const uint64_t minutes[] = {654, 999 * 60 + 59, UINT32_MAX};
  const int x3Top[] = {125, 137, 147}, x3Rule[] = {717, 671, 656};
  const int touchRule[] = {665, 647, 631};
  const EpdFontData* serif[] = {&notoserif_12_italic, &notoserif_14_italic, &notoserif_16_italic};
  for (bool touch : {false, true}) {
    for (int tier = 0; tier < 3; ++tier) {
      const bool secondary = !touch && tier == 2;
      const auto& label = faces[secondary ? tier : tier + 1];
      const auto& preferred = faces[secondary ? tier + 1 : tier + 2];
      const auto& fallback = faces[secondary ? tier : tier + 1];
      HomeCardInput cardInput;
      cardInput.screenWidth = touch ? 480 : 528;
      cardInput.top = touch ? 44 : x3Top[tier];  // main Chrome inset applied exactly once
      const int rule = touch ? touchRule[tier] : x3Rule[tier];
      cardInput.bottom = rule + 8 + faces[tier + 1].regular->advanceY;
      cardInput.titleLineHeight = faces[tier ? 4 : 3].regular->advanceY;
      cardInput.titleLines = 2;
      cardInput.authorLineHeight = faces[tier + 1].regular->advanceY;
      cardInput.excerptLineHeight = serif[tier]->advanceY;
      cardInput.rowLineHeight = faces[tier + 1].regular->advanceY;
      cardInput.metadataAboveCover = tier > 0;
      for (const auto* text : labels) {
        int w, h;
        EpdFont(label.regular).getTextDimensions(text, &w, &h);
        cardInput.statsMinWidth = std::max(cardInput.statsMinWidth, w);
      }
      cardInput.statsMinWidth = std::max(cardInput.statsMinWidth, valuePixels(preferred, "999h 59m"));
      cardInput.statsMinWidth = std::max(cardInput.statsMinWidth,
                                       valuePixels(preferred, tier ? "999h 59m/ngày" : "1h 21m/ngày")) + 8;
      cardInput.minStatsHeight = 2 + 6 * (label.regular->advanceY + preferred.regular->advanceY) + 12 +
                                2 * std::max(0, 2 * fallback.regular->advanceY - preferred.regular->advanceY);
      const auto card = homeCardLayout(cardInput);
      assert(card.ruleY == rule);
      assert(card.coverY + card.coverH < card.ruleY);
      assert(card.statsRight - card.statsX >= cardInput.statsMinWidth);
      if (tier) assert(card.authorY + cardInput.authorLineHeight <= card.coverY);
      for (const auto duration : minutes) {
        for (uint32_t days : {1u, 6u}) {
          char total[64], mean[64];
          compactstats::duration(duration, total, sizeof(total));
          compactstats::duration(duration / days, mean, sizeof(mean));
          std::string values[] = {"34%", days == 1 ? "27/12" : "365 ngày", total, std::string(mean) + "/ngày",
                                  std::to_string(days), std::to_string(UINT32_MAX)};
          HomeStatsInput in;
          in.top = card.statsTop;
          in.bottom = card.coverY + card.coverH;
          in.labelLineHeight = label.regular->advanceY;
          in.valueLineHeight = preferred.regular->advanceY;
          in.valueTail = preferred.regular->advanceY - preferred.regular->ascender;
          in.rows = (1u << HOME_STAT_COUNT) - 1;
          for (int row = 0; row < HOME_STAT_COUNT; ++row) {
            const auto& font = valuePixels(preferred, values[row]) <= card.statsRight - card.statsX ? preferred : fallback;
            std::string first = values[row], second;
            if (valuePixels(font, first) > card.statsRight - card.statsX) {
              const auto slash = first.find('/'), space = first.find(' ');
              const auto split = slash != std::string::npos && valuePixels(font, first.substr(0, slash)) <= card.statsRight - card.statsX
                                     ? slash : space;
              assert(split != std::string::npos);
              second = first.substr(split + (first[split] == ' ' ? 1 : 0));
              first.resize(split);
            }
            assert(valuePixels(font, first) <= card.statsRight - card.statsX);
            assert(valuePixels(font, second) <= card.statsRight - card.statsX);
            std::string combined = first;
            if (!second.empty()) combined += (second[0] == '/' ? "" : " ") + second;
            assert(combined == values[row]);
            in.valueHeights[row] = font.regular->advanceY * (second.empty() ? 1 : 2);
            in.valueTails[row] = font.regular->advanceY - inkBottom(font, second.empty() ? first : second);
          }
          const auto layout = homeStatsLayout(in);
          assert(layout.rows == in.rows);
          for (int row = 0; row < HOME_STAT_COUNT; ++row) {
            assert(layout.labelY[row] >= in.top);
            assert(layout.valueY[row] + in.valueHeights[row] - in.valueTails[row] <= in.bottom);
            if (row + 1 < HOME_STAT_COUNT)
              assert(layout.valueY[row] + in.valueHeights[row] <= layout.labelY[row + 1]);
          }
          assert(layout.valueY[HOME_STAT_TURNS] + in.valueHeights[HOME_STAT_TURNS] - in.valueTails[HOME_STAT_TURNS] == in.bottom);
        }
      }
      std::printf("PASS: %s tier%d six complete rows, cover%dx%d column%d body%d\n", touch ? "X4 Pro" : "X3", tier,
                  card.coverW, card.coverH, card.statsRight - card.statsX, card.coverY + card.coverH - card.statsTop);
    }
  }
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
  markedQuotePick();
  cardGeometry();
  auto touch = x3(2);
  touch.screenWidth = 480;
  touch.top = 44;
  touch.bottom = 700;
  touch.statsMinWidth = 156;
  touch.minStatsHeight = 416;
  const auto narrow = homeCardLayout(touch);
  assert(narrow.coverW == 254 && narrow.coverH == 383);
  assert(narrow.statsRight - narrow.statsX >= 156);
  assert(narrow.coverY == 44 + 33 && narrow.coverY + narrow.coverH == 44 + 416);
  HomeStatsInput wrapped;
  wrapped.top = narrow.statsTop;
  wrapped.bottom = narrow.coverY + narrow.coverH;
  wrapped.labelLineHeight = 27;
  wrapped.valueLineHeight = 33;
  wrapped.valueTail = 7;
  wrapped.rows = ALL_STATS | bit(HOME_STAT_FINISH);
  wrapped.valueHeights[HOME_STAT_TOTAL] = wrapped.valueHeights[HOME_STAT_AVERAGE] = 54;
  wrapped.valueTails[HOME_STAT_TOTAL] = wrapped.valueTails[HOME_STAT_AVERAGE] = 6;
  const auto longValues = homeStatsLayout(wrapped);
  assert(longValues.rows == wrapped.rows);
  assert(longValues.valueY[HOME_STAT_TURNS] + 33 - 7 == wrapped.bottom);
  for (int row = 0; row < HOME_STAT_COUNT - 1; ++row) {
    const int h = wrapped.valueHeights[row] ? wrapped.valueHeights[row] : 33;
    assert(longValues.valueY[row] + h <= longValues.labelY[row + 1]);
  }
  touch.metadataAboveCover = true;
  touch.titleLineHeight = 43;
  touch.authorLineHeight = 38;
  touch.statsMinWidth = 260;
  const auto above = homeCardLayout(touch);
  assert(above.titleY == touch.top && above.authorY + 38 <= above.coverY);
  assert(above.statsTop == touch.top);
  assert(above.coverY + above.coverH == above.ruleY - 15);
  assert(above.excerptLines == 0);

  statRows();
  finishRow();
  statsColumn();
  measuredSixRows();
}
