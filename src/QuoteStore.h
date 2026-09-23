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
// Most quotes the Quotes screens hold at once, as ids: 8 bytes each, so 4 KiB at the cap.
constexpr size_t MAX_QUOTES = 512;

// File name, 16 lowercase hex digits then ".json", read left to right:
//   BBBBBBBB  book key, FNV-1a 32 of the book path
//   DDDD      day code of `day`: (year - 2020) * 372 + (month - 1) * 31 + (day - 1), 0 when unknown
//   MMM       minute of the day, 0 when unknown; a later minute when the 16 slots of the
//             quote's own minute were taken (see save())
//   S         0-f, the first free slot for that book and moment
// So a directory listing alone groups quotes by book and orders them by the moment they
// were kept; no file is opened to sort, count or filter. The name never changes when the
// quote is edited, because the moment it was kept does not change.
// The 16 hex digits of a name read as one number: book key in the high 32 bits, then day
// code, minute and slot. Held instead of the name, it costs no allocation per quote.
using QuoteId = uint64_t;
std::string nameOf(QuoteId id);
// False, and `id` untouched, when `name` is not a valid quote name.
bool idOf(const std::string& name, QuoteId& id);

uint32_t bookKey(const std::string& path);
uint32_t bookKeyOfName(const std::string& name);
uint32_t bookKeyOfName(QuoteId id);
// DDDDMMM as one number, so a larger value is a later moment.
uint32_t momentOfName(const std::string& name);
uint32_t momentOfName(QuoteId id);
bool validName(const std::string& name);

// Saves a new quote. A record with the same path, text, spine and page already stored for
// that book is not written twice; an unanchored twin gains the anchor instead. When the 16
// slots of its minute are taken, the name moves to the next minute with a free slot (up to
// minute 1439, then the save fails); the record's own day and minute stay exact, so only
// the order among quotes kept that close together can be off, by minutes.
bool save(const QuoteRecord& quote);
bool load(QuoteId id, QuoteRecord& quote);
bool remove(QuoteId id);
// Rewrites record `id` in place with `updated` (same file name). The new record is
// written in full to "<name>.tmp" and closed, then the old file is removed, then the
// staged file is renamed over it. A power cut can therefore leave the old record with a
// partial "<name>.tmp" beside it, or the complete "<name>.tmp" alone; migrateNames()
// repairs both, dropping the partial file and promoting the complete one.
bool replace(QuoteId id, const QuoteRecord& updated);
// The same, by file name, for callers that still hold names.
bool load(const std::string& name, QuoteRecord& quote);
bool remove(const std::string& name);
bool replace(const std::string& name, const QuoteRecord& updated);

// Called on every visit to the Quotes screens. First repairs what a power cut left in the
// middle of a write (see replace()); that is one walk of the directory acting only on
// ".tmp" names. Then, once, renames files written before v1.0.11 (hash names) to the
// scheme above and writes the marker "/.crosspoint/quotes/.ten-v2". Returns false when a
// file could not be renamed; the marker is then not written and the next call tries again.
bool migrateNames();

// Ids of every quote, or only those of `book` when `book != 0`, newest first. Stops at
// MAX_QUOTES. Reads the directory only.
void listNames(uint32_t book, std::vector<QuoteId>& ids);

struct BookSummary {
  uint32_t book = 0;
  uint16_t count = 0;
  // Newest quote of the book, so the caller can show its date and open its record for the
  // title and path without another scan.
  QuoteId newest = 0;
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
}  // namespace quotes
