#pragma once
#include "shells/ugly/UglyMarkSeed.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "QuoteStore.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/QuoteListLayout.h"
#include "components/TenorMenuChrome.h"
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
class Epub;

class QuotesActivity final : public Activity {
 public:
  tenorchrome::X3PathIcon navigationIcon() const override { return tenorchrome::X3PathIcon::More; }
  std::string x3NavigationLabel() const override { return I18N.get(StrId::STR_QUOTES); }
  // `bookPath` empty opens the list of books; a path opens that book's quotes directly.
  // `insideReader` is true when the reader opened this screen for its own book: only then
  // does editing offer to reselect the quote on the page (finishing with QuoteEditResult).
  // `openBook` is the reader's own loaded book, passed on to the detail so it reads chapter
  // names from it instead of loading the book again.
  QuotesActivity(GfxRenderer& r, MappedInputManager& input, std::string bookPath = "", bool insideReader = false,
                 std::shared_ptr<Epub> openBook = nullptr)
      : Activity("Quotes", r, input),
        bookPath(std::move(bookPath)),
        insideReader(insideReader),
        openBook(std::move(openBook)) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  tenorchrome::X3ScreenLayout screenLayout() const;
  void drawX3Content() const;

 private:
  mutable ugly::MarkState mark_;
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
  quotelist::PageShape pageShape() const;
  int pageCount() const;
  // Reads the order for the current view, then its first page. Callers hold the lock.
  void reload();
  // Reads the records the visible page shows, and nothing else.
  void loadPage();
  void logPage() const;
  void sortByPlace();
  void cycleOrder();
  void setOrder(Order next);
  // Touch: a tap on a row opens it, a hold on a quote offers Edit and Delete, a swipe turns pages, the bar's pill
  // picks the view. True when the pass was taken.
  bool touchInput();
  // The row of the page under y, as an index into `ids` or `books`, or -1.
  int itemAt(int y) const;
  void showViewMenu();
  void showQuoteActions(int item);
  void moveCursor(int direction);
  void flipPages(int pages);
  void activate();
  void onChildDone(const ActivityResult& result);
  void leave();

  quotelist::Metrics metrics() const;
  int placeFont() const;
  // The same band, measured for the top row's own face.
  quotelist::Metrics topRowMetrics() const;
  void ensureWrapped(Block& block, int maxLines, int placeFontId) const;
  void drawTopRow(const char* label) const;
  void drawBlocks() const;
  void drawBookRows() const;
  std::string footer() const;

  std::string bookPath;
  bool insideReader = false;
  std::shared_ptr<Epub> openBook;
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
  // Touch: the view picked from the bar and the actions of a held quote.
  OptionPopup viewMenu;
  OptionPopup rowMenu;
  // Touch: where the rows of the page were drawn, for the tap that picks one (rowTops[i] to rowTops[i + 1]).
  mutable std::array<int16_t, MAX_BOOK_ROWS + 1> rowTops{};
  mutable int shownRows = 0;
};
