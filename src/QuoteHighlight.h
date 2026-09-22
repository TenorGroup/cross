#pragma once

#include <EpdFontFamily.h>
#include <Epub/Page.h>

#include <cstdint>
#include <vector>

#include "QuoteStore.h"

// Where the words of a rendered page sit in the book's visible text, so a quote saved
// on one visit can be found again on the next. The reader and the quote selector walk
// the page the same way (page elements in order, then every word of each text line),
// so an offset written when a quote was saved names the same word when it is drawn.
namespace quotes {

// One word of a rendered page. `text` points into the page's TextBlock arena and is
// valid as long as the page is.
struct PageWord {
  const char* text = nullptr;
  int16_t x = 0;
  int16_t y = 0;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  // Visible text offset of the word's first codepoint, and the word's length in
  // codepoints. One separator codepoint is counted between words, matching the single
  // space the quote selector puts between them when it builds the saved text.
  uint32_t offset = 0;
  uint16_t codepoints = 0;
};

// Walk `page` and place every word in the book's visible text. Returns the offset just
// past the last word, i.e. the exclusive end of the page's own range.
uint32_t pageWords(const Page& page, int marginLeft, int marginTop, int ascender, std::vector<PageWord>& words);

// Screen box of one word of a selection, the only geometry a highlight band needs.
struct WordBox {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
};

// One filled box of a highlight: the run of selected words that share a line, from the
// first word's left edge to the last word's right edge, so the spaces between the words
// are covered by the same fill instead of leaving a white gap per word.
struct HighlightBand {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;
};

// Padding the band adds on every side, the same slack the selector used to put around a
// single word's box.
constexpr int16_t BAND_PADDING = 1;

// Bands of the selected words `boxes`, given in reading order. A band breaks where the
// baseline changes, so a quote running over three lines comes back as three bands. The
// caller draws each band with one fill and then redraws the words over it.
void highlightBands(const std::vector<WordBox>& boxes, int lineHeight, std::vector<HighlightBand>& bands);

// Anchor of the word range [first, last] of `words`, stamped onto a quote about to be
// saved from spine `spine`.
void setAnchor(QuoteRecord& quote, const std::vector<PageWord>& words, size_t first, size_t last);

// First and last word of `words` the anchor covers. False when the anchor belongs to
// another spine or does not reach this page.
bool coveredWords(const QuoteAnchor& anchor, int spine, const std::vector<PageWord>& words, size_t& first,
                  size_t& last);

// True when at least one anchor could still be drawn at or after `pageStartOffset` in
// `spine`. Lets the reader skip the page walk on every chapter without quotes.
bool anyAnchorAtOrAfter(const std::vector<QuoteAnchor>& anchors, int spine, uint32_t pageStartOffset);

}  // namespace quotes
