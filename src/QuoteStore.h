#pragma once
#include <cstdint>
#include <string>
#include <vector>
// Where a saved quote sits in the book, in the same units the reader already uses to
// persist progress: the spine item plus a range of visible text offsets (zero-based
// count of visible Unicode codepoints from the start of the spine item). Quotes saved
// before this anchor existed have `hasAnchor == false` and are simply not drawn.
struct QuoteAnchor {
  int32_t spine = 0;
  uint32_t start = 0;
  uint32_t end = 0;
};
struct QuoteRecord {
  std::string path, title, text;
  int spine = 0, page = 0;
  uint32_t day = 0;
  // Minute of the local day the quote was kept, 0-1439. Records written before the clock
  // was stamped, and records kept while the clock could not be read, carry NO_MINUTE and
  // show the date alone.
  uint16_t minute = 0xFFFF;
  bool hasAnchor = false;
  uint32_t anchorStart = 0, anchorEnd = 0;
};
namespace quotes {
constexpr uint16_t NO_MINUTE = 0xFFFF;
constexpr size_t MAX_BYTES = 1024;
constexpr size_t PAGE_SIZE = 20;
// Anchors held in RAM while a book is open: 12 bytes each, so the whole list stays
// under a kilobyte. Quote files are scanned once per book open, never per page turn.
constexpr size_t MAX_BOOK_ANCHORS = 64;
constexpr size_t MAX_ANCHOR_SCAN = 256;
bool save(const QuoteRecord& quote);
bool load(const std::string& name, QuoteRecord& quote);
void list(const std::string& boundary, bool previous, std::vector<std::string>& names);
// Anchors of every anchored quote saved for `bookPath`, for drawing highlights while
// reading. Stops after MAX_ANCHOR_SCAN files or MAX_BOOK_ANCHORS matches.
void loadAnchors(const std::string& bookPath, std::vector<QuoteAnchor>& anchors);
}  // namespace quotes
