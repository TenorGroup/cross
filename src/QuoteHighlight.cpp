#include "QuoteHighlight.h"

#include <Utf8.h>

#include <algorithm>

namespace quotes {
namespace {

// Pages hold roughly two hundred words; reserving once keeps the walk from
// re-growing the vector under a render that is already holding the framebuffer.
constexpr size_t WORDS_PER_PAGE_HINT = 192;

uint16_t codepointCount(const char* text) {
  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  uint32_t count = 0;
  while (*cursor) {
    utf8NextCodepoint(&cursor);
    count++;
  }
  return static_cast<uint16_t>(count);
}

}  // namespace

uint32_t pageWords(const Page& page, const int marginLeft, const int marginTop, const int ascender,
                   std::vector<PageWord>& words) {
  words.clear();
  words.reserve(WORDS_PER_PAGE_HINT);
  uint32_t offset = page.visibleTextOffset;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (!block || !block->valid()) continue;
    const int rubyShift = block->getRubyShift(ascender);
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const char* text = block->wordText(i);
      // Same word filter the quote selector uses in quote mode, so both walks
      // produce the same list in the same order.
      if (!*text) continue;
      PageWord word;
      word.text = text;
      word.x = static_cast<int16_t>(line->xPos + block->wordXpos(i) + marginLeft);
      word.y = static_cast<int16_t>(line->yPos + marginTop + rubyShift);
      word.style = block->wordStyle(i);
      word.offset = offset;
      word.codepoints = codepointCount(text);
      // One separator codepoint per gap, matching the single space the selector
      // puts between words when it builds the quote text.
      offset += static_cast<uint32_t>(word.codepoints) + 1;
      words.push_back(word);
    }
  }
  if (words.empty()) return page.visibleTextOffset;
  return words.back().offset + words.back().codepoints;
}

void highlightBands(const std::vector<WordBox>& boxes, const int lineHeight, std::vector<HighlightBand>& bands) {
  bands.clear();
  const auto flush = [&](const int16_t left, const int16_t right, const int16_t top) {
    HighlightBand band;
    band.x = static_cast<int16_t>(left - BAND_PADDING);
    band.y = static_cast<int16_t>(top - BAND_PADDING);
    band.width = static_cast<int16_t>(right - left + BAND_PADDING * 2);
    band.height = static_cast<int16_t>(lineHeight + BAND_PADDING * 2);
    bands.push_back(band);
  };
  bool open = false;
  int16_t left = 0, right = 0, top = 0;
  for (const auto& box : boxes) {
    // A new baseline ends the band: the words of one line become one fill, and the fill
    // reaches from the leftmost to the rightmost edge on that line, so the spaces between
    // the words are inside it.
    if (open && box.y != top) {
      flush(left, right, top);
      open = false;
    }
    const int16_t boxRight = static_cast<int16_t>(box.x + box.width);
    if (!open) {
      left = box.x;
      right = boxRight;
      top = box.y;
      open = true;
      continue;
    }
    if (box.x < left) left = box.x;
    if (boxRight > right) right = boxRight;
  }
  if (open) flush(left, right, top);
}

void setAnchor(QuoteRecord& quote, const std::vector<PageWord>& words, const size_t first, const size_t last) {
  if (words.empty() || first > last || last >= words.size()) return;
  quote.hasAnchor = true;
  quote.anchorStart = words[first].offset;
  quote.anchorEnd = words[last].offset + words[last].codepoints;
}

bool coveredWords(const QuoteAnchor& anchor, const int spine, const std::vector<PageWord>& words, size_t& first,
                  size_t& last) {
  if (anchor.spine != spine || anchor.end <= anchor.start) return false;
  bool found = false;
  for (size_t i = 0; i < words.size(); i++) {
    const uint32_t wordStart = words[i].offset;
    const uint32_t wordEnd = wordStart + words[i].codepoints;
    if (wordEnd <= anchor.start || wordStart >= anchor.end) continue;
    if (!found) {
      first = i;
      found = true;
    }
    last = i;
  }
  return found;
}

bool anyAnchorAtOrAfter(const std::vector<QuoteAnchor>& anchors, const int spine, const uint32_t pageStartOffset) {
  for (const auto& anchor : anchors) {
    if (anchor.spine == spine && anchor.end > pageStartOffset) return true;
  }
  return false;
}

size_t wordCount(const std::string& text) {
  return text.empty() ? 0 : static_cast<size_t>(std::count(text.begin(), text.end(), ' ')) + 1;
}

// Kept beside the page walk so the codepoints a trim drops are counted by the same function
// that placed the words, which is what keeps the moved anchor on the words that are left.
bool trimWords(QuoteRecord& quote, const size_t front, const size_t back) {
  const size_t words = wordCount(quote.text);
  if (front >= words || back >= words - front) return false;
  const std::string& text = quote.text;
  // Each dropped word takes its one separator with it, the same gap pageWords counts.
  size_t begin = 0;
  uint32_t droppedFront = 0;
  for (size_t i = 0; i < front; i++) {
    const size_t space = text.find(' ', begin);
    droppedFront += codepointCount(text.substr(begin, space - begin).c_str()) + 1u;
    begin = space + 1;
  }
  size_t end = text.size();
  uint32_t droppedBack = 0;
  for (size_t i = 0; i < back; i++) {
    const size_t space = text.rfind(' ', end - 1);
    droppedBack += codepointCount(text.substr(space + 1, end - space - 1).c_str()) + 1u;
    end = space;
  }
  quote.text = text.substr(begin, end - begin);
  if (quote.hasAnchor) {
    quote.anchorStart += droppedFront;
    quote.anchorEnd -= droppedBack;
  }
  return true;
}

}  // namespace quotes
