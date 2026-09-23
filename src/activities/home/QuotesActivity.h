#pragma once
#include <array>
#include <string>
#include <vector>

#include "QuoteStore.h"
#include "activities/Activity.h"
#include "components/QuoteListLayout.h"
#include "util/ButtonNavigator.h"

// Saved quotes, two levels deep. Opened from Home it lists the books that have quotes
// (mockup B1); its top row turns that into every quote, newest or oldest first (A1-A4).
// Opened for one book, by picking it here or from the reader's tools, it lists that
// book's quotes (B2), newest, oldest, or in the order they stand in the book. Select on a
// quote opens it whole (QuoteDetailActivity).
//
// Only the page on screen is ever read from the card: the order comes from file names
// alone (QuoteStore.h), and each visible quote or book row opens one record. Every font
// here lives in flash, so nothing on screen is measured through the card.
class QuotesActivity final : public Activity {
 public:
  // `bookPath` empty opens the list of books; a path opens that book's quotes directly.
  // `insideReader` is true when the reader opened this screen for its own book: only then
  // does editing offer to reselect the quote on the page (finishing with QuoteEditResult).
  QuotesActivity(GfxRenderer& r, MappedInputManager& input, std::string bookPath = "", bool insideReader = false)
      : Activity("Quotes", r, input), bookPath(std::move(bookPath)), insideReader(insideReader) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // What the list shows. The first three belong to the top level and are cycled from its
  // top row; the last three belong to one book.
  enum class Order : uint8_t { Books, AllNewest, AllOldest, BookNewest, BookOldest, BookPage };

  // One quote on the visible page. The record is read when the page is loaded; wrapping
  // waits until the block is drawn, the pattern the list has always used.
  struct Block {
    quotes::QuoteId id = 0;
    bool readable = false;
    bool wrapped = false;
    std::string preview;
    std::vector<std::string> lines;
    std::string title;
    std::string place;
  };
  struct BookRow {
    std::string title;
    std::string path;
    std::string latest;
    int count = 0;
  };

  // More book rows than the panel at the smallest UI size shows (seven), so the cap only
  // bounds the array.
  static constexpr int MAX_BOOK_ROWS = 12;

  bool bookLevel() const { return !bookPath.empty(); }
  bool showsBooks() const { return order == Order::Books; }
  int itemCount() const;
  int perPage() const;
  int pageCount() const;
  // Reads the order for the current view, then its first page. Callers hold the lock.
  void reload();
  // Reads the records the visible page shows, and nothing else.
  void loadPage();
  void logPage() const;
  void sortByPlace();
  void cycleOrder();
  void moveCursor(int direction);
  void flipPages(int pages);
  void activate();
  void onChildDone(const ActivityResult& result);
  void leave();

  quotelist::Metrics metrics() const;
  // The same band, measured for the top row's own face.
  quotelist::Metrics topRowMetrics() const;
  void ensureWrapped(Block& block) const;
  void drawTopRow(const char* label) const;
  void drawBlocks() const;
  void drawBookRows() const;
  std::string footer() const;

  std::string bookPath;
  bool insideReader = false;
  uint32_t book = 0;
  std::string bookTitle;
  Order order = Order::Books;
  std::vector<quotes::QuoteId> ids;
  std::vector<quotes::BookSummary> books;
  int quoteTotal = 0;
  int bookTotal = 0;
  // -1 is the top row; otherwise an index into `ids` or `books`.
  int selected = 0;
  int page = 0;
  mutable std::array<Block, quotelist::BLOCKS_PER_PAGE> blocks;
  int blockCount = 0;
  std::array<BookRow, MAX_BOOK_ROWS> rows;
  int rowCount = 0;
  // A quote was deleted or edited below this screen, so whoever opened it reloads too.
  bool storeChanged = false;
  ButtonNavigator cursorNavigator;
};
