#pragma once
#include <cstdint>

// Shape of the Quotes detail screen (mockup D2: the big opening quote mark, the quote
// itself, a short rule, then the source). Pure arithmetic, no renderer, so the whole
// layout is checked on a desktop (test/ui_layout/quote_detail.cpp) instead of by eye on
// the panel.
namespace quotedetail {

// Body column: the same margins on every screen this activity can show.
constexpr int16_t BODY_X0 = 44;
constexpr int16_t BODY_RIGHT_INSET = 40;
// The big opening quote mark (components/QuoteMarkGlyph.h) hangs the same few pixels
// left of the body column the way the list's own hanging quote does.
constexpr int16_t GLYPH_X_BLEED = 4;
// Gap from the bottom of the glyph box to the quote's first line.
constexpr int16_t GLYPH_GAP = 20;
// Right inset for the "2/5" position text in the header.
constexpr int16_t HEADER_RIGHT_INSET = 18;

// The meta block under the quote: book title (Noto Serif italic 12), then chapter, the
// day and time, and the page, each in Geist 10 (mockup D2). Every gap here is a
// fixed design choice, not derived from a font's own metrics, because this block's fonts
// never change with the reader's settings the way the quote body's does.
constexpr int16_t META_RULE_GAP = 30;    // last body line to the short rule
constexpr int16_t META_RULE_WIDTH = 48;
constexpr int16_t META_RULE_HEIGHT = 2;
constexpr int16_t META_TITLE_GAP = 22;   // rule to the title line
constexpr int16_t META_LIST_GAP = 38;    // title line to the first of the three meta lines
constexpr int16_t META_LINE_STEP = 32;   // step between the three meta lines
// Room the last meta line ("Trang N") itself needs below its own top, so it does not
// crowd the button hints under it.
constexpr int16_t META_BOTTOM_MARGIN = 24;

struct Metrics {
  int16_t bandX = 0;
  int16_t bandWidth = 0;
  int16_t bandTop = 0;     // top of the body area, below the header
  int16_t bandBottom = 0;  // bottom of the body area, above the button hints
  int16_t glyphWidth = 0;  // QUOTE_MARK_GLYPH_WIDTH (components/QuoteMarkGlyph.h)
  int16_t glyphHeight = 0; // QUOTE_MARK_GLYPH_HEIGHT
  int16_t lineHeight18 = 0;  // Noto Serif 18, the first size tried
  int16_t lineHeight16 = 0;  // Noto Serif 16, tried when 18 does not fit
};

// x0/x1 for wrapping and drawing the quote body and the source lines under it.
int16_t bodyX0(const Metrics& m);
int16_t bodyX1(const Metrics& m);

// Placement for the big opening quote mark, drawn with
// renderer.drawImage(kQuoteMarkGlyphBitmap, glyphX, glyphY, QUOTE_MARK_DRAW_WIDTH, QUOTE_MARK_DRAW_HEIGHT).
int16_t glyphX(const Metrics& m);
int16_t glyphY(const Metrics& m);
// Top of the quote's first line: below the glyph box and its gap.
int16_t bodyTop(const Metrics& m);

// How many lines of the given line height fit before the button hints, with no meta
// block reserved (every in-quote page except the last).
int linesPerPage(int16_t lineHeight, const Metrics& m);
// The same, with the meta block's own height reserved below (the last in-quote page).
int linesPerLastPage(int16_t lineHeight, const Metrics& m);

struct FontChoice {
  bool useSize18 = true;
  int pages = 1;
};

// `lineCount18`/`lineCount16` are the caller's own wrap of the quote at each size
// (renderer.wrappedText). Tries 18 first; drops to 16 when 18 would need paging; paginates
// at 16 when even 16 overflows a single page.
FontChoice chooseFont(int lineCount18, int lineCount16, const Metrics& m);

struct MetaBlock {
  int16_t x = 0;
  int16_t ruleY = 0;
  int16_t titleY = 0;
  int16_t chapterY = 0;
  int16_t whenY = 0;
  int16_t pageY = 0;
};

// Meta block placed right under the quote's last line on the last in-quote page.
// `bodyBottomY` is one line-step past the last body line actually drawn (bodyTop(m) plus
// the number of lines on that page times the chosen line height).
MetaBlock metaBlock(const Metrics& m, int16_t bodyBottomY);
// Total height metaBlock() reserves below `bodyBottomY`, floor to floor of the "Trang N"
// line's own room: what linesPerLastPage() takes off the full body area.
int16_t metaBlockHeight();

// Right edge to anchor-right the "2/5" position text against, in the screen's own header.
int16_t headerPositionRight(const Metrics& m);

}  // namespace quotedetail
