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
// Anchors held in RAM while a book is open: 12 bytes each. Quote files are scanned once
// per book open, never per page turn, and only the files of that book are opened.
constexpr size_t MAX_BOOK_ANCHORS = 128;
// Most names the Quotes screens hold at once: 16 bytes each, so 8 KiB at the cap.
constexpr size_t MAX_QUOTES = 512;

// File name, 16 lowercase hex digits then ".json", read left to right:
//   BBBBBBBB  book key, FNV-1a 32 of the book path
//   DDDD      day code of `day`: (year - 2020) * 372 + (month - 1) * 31 + (day - 1), 0 when unknown
//   MMM       minute of the day, 0 when unknown
//   S         0-f, the first free slot for that book and moment
// So a directory listing alone groups quotes by book and orders them by the moment they
// were kept; no file is opened to sort, count or filter. The name never changes when the
// quote is edited, because the moment it was kept does not change.
uint32_t bookKey(const std::string& path);
uint32_t bookKeyOfName(const std::string& name);
// DDDDMMM as one number, so a larger value is a later moment.
uint32_t momentOfName(const std::string& name);
bool validName(const std::string& name);

// Saves a new quote. A record with the same path, text, spine and page already stored for
// that book is not written twice; an unanchored twin gains the anchor instead.
bool save(const QuoteRecord& quote);
bool load(const std::string& name, QuoteRecord& quote);
bool remove(const std::string& name);
// Rewrites record `name` in place with `updated` (same file name). Staged through a
// temporary file so a power cut leaves either the old or the new record, never neither.
bool replace(const std::string& name, const QuoteRecord& updated);

// One pass that renames files written before v1.0.11 (hash names) to the scheme above,
// then writes the marker "/.crosspoint/quotes/.ten-v2". Cheap no-op once the marker exists.
// Returns false when a file could not be renamed; the marker is then not written and the
// next call tries again.
bool migrateNames();

// Names of every quote, or only those of `book` when `book != 0`, newest first. Stops at
// MAX_QUOTES. Reads the directory only.
void listNames(uint32_t book, std::vector<std::string>& names);

struct BookSummary {
  uint32_t book = 0;
  uint16_t count = 0;
  // Newest name of the book, so the caller can show its date and open its record for the
  // title and path without another scan.
  std::string newest;
};
// One entry per book that has quotes, newest book first. Reads the directory only.
void listBooks(std::vector<BookSummary>& books);

// Anchors of every anchored quote saved for `bookPath`, for drawing highlights while
// reading. Opens only the files whose name carries that book's key.
void loadAnchors(const std::string& bookPath, std::vector<QuoteAnchor>& anchors);

// Drops `front` words from the start and `back` words from the end of `quote.text` (words
// are separated by single spaces, as the selector joins them) and moves the anchor by the
// same number of codepoints. False, and `quote` untouched, when fewer than one word would
// be left.
bool trimWords(QuoteRecord& quote, size_t front, size_t back);
// Number of words trimWords counts in `text`.
size_t wordCount(const std::string& text);

// v1.0.10 paging, kept only until QuotesActivity moves to listNames(); removed at merge.
constexpr size_t PAGE_SIZE = 20;
constexpr size_t MAX_ANCHOR_SCAN = 256;
void list(const std::string& boundary, bool previous, std::vector<std::string>& names);
}  // namespace quotes
