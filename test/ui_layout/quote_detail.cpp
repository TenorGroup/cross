// Layout of the Quotes detail screen (mockup D2: the big opening quote mark, the quote,
// a short rule, then the source). Checked here because the font-choice arithmetic and the
// meta block's placement are exactly what silently broke in v1.0.10 (DEBT: a 60 second
// screen from measuring text in a font that might live on the SD card).
#include <cassert>
#include <cstdio>

#include "components/QuoteDetailLayout.h"
#include "components/QuoteMarkGlyph.h"

namespace {

// X3 panel, portrait, with the header and the button hints already taken off the band.
constexpr int16_t BAND_X = 0;
constexpr int16_t BAND_WIDTH = 528;
constexpr int16_t BAND_TOP = 0;
constexpr int16_t BAND_BOTTOM = 650;

// Noto Serif 18 and 16, as the generated font measures at 150 DPI (ascent + descent).
constexpr int16_t LINE_18 = 53;
constexpr int16_t LINE_16 = 46;

quotedetail::Metrics metrics() {
  return {BAND_X, BAND_WIDTH, BAND_TOP, BAND_BOTTOM, QUOTE_MARK_GLYPH_WIDTH, QUOTE_MARK_GLYPH_HEIGHT, LINE_18,
          LINE_16};
}

}  // namespace

int main() {
  const auto m = metrics();

  // The glyph header's two pairs of constants: the size it reads at, and the size it is
  // packed at after the pre-rotation GfxRenderer::drawImage() needs (see the header's own
  // comment). A 90 degree rotation swaps width and height; it must not silently change
  // area or drop a row.
  assert(QUOTE_MARK_GLYPH_WIDTH == 36);
  assert(QUOTE_MARK_GLYPH_HEIGHT == 27);
  assert(QUOTE_MARK_DRAW_WIDTH == QUOTE_MARK_GLYPH_HEIGHT);
  assert(QUOTE_MARK_DRAW_HEIGHT == QUOTE_MARK_GLYPH_WIDTH);
  constexpr int16_t rowBytes = (QUOTE_MARK_DRAW_WIDTH + 7) / 8;
  assert(static_cast<int>(sizeof(kQuoteMarkGlyphBitmap)) == rowBytes * QUOTE_MARK_DRAW_HEIGHT);
  assert(sizeof(kQuoteMarkGlyphBitmap) < 1024);

  // Body column and the glyph box above it.
  assert(quotedetail::bodyX0(m) == BAND_X + quotedetail::BODY_X0);
  assert(quotedetail::bodyX1(m) == BAND_X + BAND_WIDTH - quotedetail::BODY_RIGHT_INSET);
  assert(quotedetail::glyphX(m) == quotedetail::bodyX0(m) - quotedetail::GLYPH_X_BLEED);
  assert(quotedetail::glyphY(m) == BAND_TOP);
  assert(quotedetail::bodyTop(m) == BAND_TOP + QUOTE_MARK_GLYPH_HEIGHT + quotedetail::GLYPH_GAP);

  // A two-line quote (mockup D2's own sample) reads at 18: the research note's own
  // measurement ("khoảng 8 dòng") falls out of the same arithmetic this asserts.
  const auto short_choice = quotedetail::chooseFont(2, 2, m);
  assert(short_choice.useSize18 == true);
  assert(short_choice.pages == 1);

  // Past what 18 holds on one page but not what 16 holds: drops to 16, still one page.
  const int lastMax18 = quotedetail::linesPerLastPage(LINE_18, m);
  const int lastMax16 = quotedetail::linesPerLastPage(LINE_16, m);
  assert(lastMax18 > 0 && lastMax16 > lastMax18);
  const auto dropped = quotedetail::chooseFont(lastMax18 + 1, lastMax16, m);
  assert(dropped.useSize18 == false);
  assert(dropped.pages == 1);

  // A 1.024 byte quote (the store's own ceiling) wraps to far more lines than even 16pt
  // holds on one page: it paginates instead of overflowing the band.
  const int longLineCount = 60;
  const auto paged = quotedetail::chooseFont(longLineCount, longLineCount, m);
  assert(paged.useSize18 == false);
  assert(paged.pages > 1);
  const int perPage = quotedetail::linesPerPage(LINE_16, m);
  const int expectedPages = 1 + (longLineCount - lastMax16 + perPage - 1) / perPage;
  assert(paged.pages == expectedPages);

  // The meta block, placed after the last body line of the page that holds it, never
  // reaches into the button hints under it: linesPerLastPage() reserved exactly its own
  // height, so the worst case (a page filled to that line count) still leaves the "Trang
  // N" line's own room above the band's bottom.
  const int16_t bodyBottomAtCapacity = static_cast<int16_t>(quotedetail::bodyTop(m) + lastMax16 * LINE_16);
  const auto meta = quotedetail::metaBlock(m, bodyBottomAtCapacity);
  assert(meta.x == quotedetail::bodyX0(m));
  assert(meta.titleY > meta.ruleY);
  assert(meta.chapterY > meta.titleY);
  assert(meta.whenY - meta.chapterY == quotedetail::META_LINE_STEP);
  assert(meta.pageY - meta.whenY == quotedetail::META_LINE_STEP);
  assert(meta.pageY + quotedetail::META_BOTTOM_MARGIN <= BAND_BOTTOM);
  assert(bodyBottomAtCapacity + quotedetail::metaBlockHeight() <= BAND_BOTTOM);

  // The "2/5" position text in the header is anchored to the band's own right edge.
  assert(quotedetail::headerPositionRight(m) == BAND_X + BAND_WIDTH - quotedetail::HEADER_RIGHT_INSET);

  puts("PASS: quote detail picks 18 or 16, paginates a long quote, and keeps the meta block "
       "off the button hints");
}
