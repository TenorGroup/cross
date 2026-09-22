#pragma once

#include <cstddef>
#include <vector>

#include "QuoteHighlight.h"
#include "QuoteStore.h"

namespace quotes {

// Words [first, last] of a rendered page that `quote` was saved over, so the selector can
// reopen with the old range already marked and a single Confirm saves it unchanged. `words`
// is the page walk of pageWords() and `spine` the spine item the page belongs to.
//
// False when the quote has no anchor, belongs to another spine item, or does not lie wholly
// on this page. The last case happens after the text was laid out again at another size:
// marking only the part on this page would silently save a shorter quote.
inline bool reselectRange(const QuoteRecord& quote, const int spine, const std::vector<PageWord>& words,
                          size_t& first, size_t& last) {
  if (!quote.hasAnchor || words.empty()) return false;
  if (words.front().offset > quote.anchorStart) return false;
  if (words.back().offset + words.back().codepoints < quote.anchorEnd) return false;
  return coveredWords(QuoteAnchor{static_cast<int32_t>(quote.spine), quote.anchorStart, quote.anchorEnd}, spine,
                      words, first, last);
}

}  // namespace quotes
