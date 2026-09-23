#include "QuotesActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "QuoteDetailActivity.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Every face on these screens lives in flash (see the class comment): the quote body in
// Noto Serif 14, the numbers and book rows in the UI body tier, the top row in the UI
// subtitle tier, the place and date lines in the caption tier. The book title over the
// place line is bold (mockup A1), and the caption tier has no bold at the smallest UI
// size, so the title takes the subtitle tier's bold instead.
constexpr int BODY_FONT_ID = NOTOSERIF_14_FONT_ID;
constexpr int NUMBER_FONT_ID = UI_12_FONT_ID;
constexpr int TOP_ROW_FONT_ID = UI_10_FONT_ID;
constexpr int SOURCE_FONT_ID = SMALL_FONT_ID;
constexpr int SOURCE_TITLE_FONT_ID = UI_10_FONT_ID;
// Left inset and right reserve of the header row (components/TenorMenuChrome.cpp).
constexpr int HEADER_SIDE = 18;

// Longest part of a quote a list block keeps to wrap: three lines of Noto Serif 14 hold
// well under this, and the whole quote is read in the detail view from the card again.
constexpr size_t PREVIEW_BYTES = 240;

// Holding a side button this long jumps ten pages instead of one.
constexpr unsigned long HOLD_MS = 700;
constexpr int JUMP_PAGES = 10;

// Room between the last row and the footer line under it.
constexpr int FOOTER_GAP = 8;
// The top row's mark reaches this far past its own text on each side (mockup A4).
constexpr int TOP_ROW_MARK_PAD = 6;
constexpr int BOOK_ROW_RADIUS = 8;

constexpr char OPEN_QUOTE[] = "\xe2\x80\x9c";
constexpr char CLOSE_QUOTE[] = "\xe2\x80\x9d";
constexpr char ELLIPSIS[] = "\xe2\x80\xa6";

// Kept until power off, the way the research note settled it; not written to the card.
uint8_t rememberedTop = 0;   // Order::Books
uint8_t rememberedBook = 3;  // Order::BookNewest

std::string formatted(const char* format, const QuoteRecord& quote) {
  char line[96];
  snprintf(line, sizeof(line), format, quote.spine + 1, quote.page + 1, static_cast<unsigned>(quote.day % 100),
           static_cast<unsigned>(quote.day / 100 % 100), static_cast<unsigned>(quote.day / 10000));
  return line;
}

std::string latestLine(const QuoteRecord& quote) {
  char line[64];
  snprintf(line, sizeof(line), tr(STR_QUOTES_BOOK_LATEST), static_cast<unsigned>(quote.day % 100),
           static_cast<unsigned>(quote.day / 100 % 100), static_cast<unsigned>(quote.day / 10000));
  return line;
}

[[maybe_unused]] const char* orderName(const uint8_t order) {
  static constexpr const char* names[] = {"books", "newest", "oldest", "newest", "oldest", "page"};
  return names[order];
}

}  // namespace

using quotetext::clipped;
using quotetext::endsWithCloseQuote;
using quotetext::tidyEllipsis;

void QuotesActivity::onEnter() {
  Activity::onEnter();
  {
    RenderLock lock(*this);
    // Every visit: it repairs a write a power cut interrupted, and renames v1.0.10 files
    // the first time (QuoteStore.h). Both are one walk of the directory at most.
    quotes::migrateNames();
    if (bookLevel()) {
      book = quotes::bookKey(bookPath);
      order = static_cast<Order>(rememberedBook);
    } else {
      order = static_cast<Order>(rememberedTop);
    }
    reload();
  }
  requestUpdate();
}

int QuotesActivity::itemCount() const {
  return showsBooks() ? static_cast<int>(books.size()) : static_cast<int>(ids.size());
}

int QuotesActivity::perPage() const {
  return showsBooks() ? std::min(MAX_BOOK_ROWS, quotelist::bookRowsPerPage(metrics())) : quotelist::BLOCKS_PER_PAGE;
}

int QuotesActivity::pageCount() const { return quotelist::bookPageCount(itemCount(), perPage()); }

quotelist::Metrics QuotesActivity::metrics() const {
  quotelist::Metrics m;
  m.bandX = 0;
  m.bandWidth = static_cast<int16_t>(renderer.getScreenWidth());
  m.bandTop = static_cast<int16_t>(quotesHeaderBottom());
  m.bandBottom = static_cast<int16_t>(tenorchrome::tipY(renderer) - FOOTER_GAP);
  m.bodyLineHeight = static_cast<int16_t>(renderer.getLineHeight(BODY_FONT_ID));
  m.numberLineHeight = static_cast<int16_t>(renderer.getLineHeight(NUMBER_FONT_ID));
  // Above the place line, the top level's blocks carry a book title in the taller title
  // face; the layout steps both source lines by this one height.
  m.smallLineHeight = static_cast<int16_t>(
      renderer.getLineHeight(!bookLevel() && !showsBooks() ? SOURCE_TITLE_FONT_ID : SOURCE_FONT_ID));
  return m;
}

quotelist::Metrics QuotesActivity::topRowMetrics() const {
  auto m = metrics();
  m.smallLineHeight = static_cast<int16_t>(renderer.getLineHeight(TOP_ROW_FONT_ID));
  return m;
}

// Book order: by spine item, then by the anchor inside it. A quote kept before anchors
// existed has only its page, which is not comparable with an anchor offset, so within a
// spine item those come after the anchored ones, by page; one that cannot be read comes
// last. Each rule compares whole keys, so the order is total.
// Only this book's records are opened, once, when this order is chosen.
void QuotesActivity::sortByPlace() {
  struct Place {
    quotes::QuoteId id;
    int32_t spine;
    uint8_t group;
    uint32_t at;
  };
  std::vector<Place> places;
  places.reserve(ids.size());
  for (const auto id : ids) {
    QuoteRecord quote;
    if (!quotes::load(id, quote)) {
      places.push_back({id, INT32_MAX, 2, 0});
      continue;
    }
    if (bookTitle.empty()) bookTitle = clipped(quote.title, 256);
    places.push_back({id, quote.spine, static_cast<uint8_t>(quote.hasAnchor ? 0 : 1),
                      quote.hasAnchor ? quote.anchorStart : static_cast<uint32_t>(quote.page)});
  }
  const auto before = [](const Place& a, const Place& b) {
    if (a.spine != b.spine) return a.spine < b.spine;
    if (a.group != b.group) return a.group < b.group;
    if (a.at != b.at) return a.at < b.at;
    // Same place: the one kept first reads first.
    const auto momentA = static_cast<uint32_t>(a.id), momentB = static_cast<uint32_t>(b.id);
    return momentA != momentB ? momentA < momentB : a.id < b.id;
  };
  // Insertion sort: at most MAX_BOOK_ANCHORS-sized books in practice, milliseconds next to
  // the records just read, and a fraction of std::sort's code in a flash that is nearly full.
  for (size_t i = 1; i < places.size(); ++i) {
    const Place place = places[i];
    size_t j = i;
    for (; j > 0 && before(place, places[j - 1]); --j) places[j] = places[j - 1];
    places[j] = place;
  }
  for (size_t i = 0; i < places.size(); ++i) ids[i] = places[i].id;
}

void QuotesActivity::reload() {
  ids.clear();
  books.clear();
  if (showsBooks()) {
    quotes::listBooks(books);
    bookTotal = static_cast<int>(books.size());
    quoteTotal = 0;
    for (const auto& summary : books) quoteTotal += summary.count;
  } else {
    quotes::listNames(bookLevel() ? book : 0, ids);
    if (order == Order::AllOldest || order == Order::BookOldest) std::reverse(ids.begin(), ids.end());
    if (order == Order::BookPage) sortByPlace();
    quoteTotal = static_cast<int>(ids.size());
    std::vector<uint32_t> keys;
    for (const auto id : ids) {
      const uint32_t key = quotes::bookKeyOfName(id);
      if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    }
    bookTotal = static_cast<int>(keys.size());
    if (bookLevel() && bookTitle.empty() && !ids.empty()) {
      QuoteRecord quote;
      if (quotes::load(ids.front(), quote)) bookTitle = clipped(quote.title, 256);
    }
  }
  const int count = itemCount();
  if (selected >= count) selected = count - 1;
  if (count == 0) selected = 0;
  page = selected >= 0 ? selected / perPage() : std::min(page, pageCount() - 1);
  loadPage();
}

void QuotesActivity::loadPage() {
  const int first = page * perPage();
  const int last = std::min(itemCount(), first + perPage());
  if (showsBooks()) {
    rowCount = 0;
    for (int i = first; i < last && rowCount < MAX_BOOK_ROWS; ++i) {
      const auto& summary = books[i];
      BookRow& row = rows[rowCount++];
      row = BookRow{};
      row.count = summary.count;
      QuoteRecord quote;
      if (quotes::load(summary.newest, quote)) {
        row.title = clipped(quote.title.empty() ? quote.path : quote.title, 256);
        row.path = quote.path;
        row.latest = latestLine(quote);
      } else {
        row.title = tr(STR_QUOTES_UNREADABLE);
      }
    }
    logPage();
    return;
  }
  blockCount = 0;
  for (int i = first; i < last; ++i) {
    Block& block = blocks[blockCount++];
    block = Block{};
    block.id = ids[i];
    QuoteRecord quote;
    block.readable = quotes::load(block.id, quote);
    if (block.readable) {
      block.preview = clipped(quote.text, PREVIEW_BYTES);
      if (block.preview.size() < quote.text.size()) block.preview += ELLIPSIS;
      block.title = clipped(quote.title, 256);
      block.place = formatted(tr(STR_QUOTES_LIST_PLACE), quote);
    }
  }
  logPage();
}

// What the page shows, by name, for the simulator journeys that check the order and the
// paging (test/reading_stats_simulator/test_quotes_v1011.py). Simulator only: the flash of
// every device build is nearly full, so none of them carries it.
void QuotesActivity::logPage() const {
#ifdef SIMULATOR
  const int first = page * perPage();
  const int last = std::min(itemCount(), first + perPage());
  std::string shown;
  char key[24];
  for (int i = first; i < last; ++i) {
    if (showsBooks())
      snprintf(key, sizeof(key), " %08x", static_cast<unsigned>(books[i].book));
    else
      snprintf(key, sizeof(key), " %016llx", static_cast<unsigned long long>(ids[i]));
    shown += key;
  }
  if (showsBooks())
    LOG_DBG("QTS", "Quote books page %d/%d of %d books, %d quotes:%s", page + 1, pageCount(), bookTotal, quoteTotal,
            shown.c_str());
  else
    LOG_DBG("QTS", "Quote list %s %s page %d/%d of %d:%s", bookLevel() ? "book" : "all",
            orderName(static_cast<uint8_t>(order)), page + 1, pageCount(), itemCount(), shown.c_str());
#endif
}

void QuotesActivity::cycleOrder() {
  Order next;
  switch (order) {
    case Order::Books: next = Order::AllNewest; break;
    case Order::AllNewest: next = Order::AllOldest; break;
    case Order::AllOldest: next = Order::Books; break;
    case Order::BookNewest: next = Order::BookOldest; break;
    case Order::BookOldest: next = Order::BookPage; break;
    case Order::BookPage:
    default: next = Order::BookNewest; break;
  }
#ifdef SIMULATOR
  LOG_DBG("QTS", "Quote view %s %s", bookLevel() ? "book" : "all", orderName(static_cast<uint8_t>(next)));
#endif
  {
    RenderLock lock(*this);
    order = next;
    (bookLevel() ? rememberedBook : rememberedTop) = static_cast<uint8_t>(next);
    selected = -1;
    page = 0;
    reload();
  }
  requestUpdate();
}

// One linear walk: the top row, then every row across every page, wrapping at both ends.
// The page follows the cursor, so stepping past a page's last row turns to the next page
// and stepping back past its first row turns back.
void QuotesActivity::moveCursor(const int direction) {
  const int count = itemCount();
  if (count <= 0) return;
  const int first = page * perPage();
  int next = selected;
  int nextPage = page;
  if (direction > 0) {
    if (selected < 0) {
      next = first;
    } else if (selected + 1 >= count) {
      next = -1;
      nextPage = 0;
    } else {
      next = selected + 1;
    }
  } else if (selected < 0) {
    next = count - 1;
  } else if (selected == 0) {
    next = -1;
  } else {
    next = selected - 1;
  }
  if (next >= 0) nextPage = next / perPage();
  if (nextPage != page) {
    RenderLock lock(*this);
    selected = next;
    page = nextPage;
    loadPage();
  } else {
    selected = next;
  }
  requestUpdate();
}

void QuotesActivity::flipPages(const int pages) {
  const int target = std::max(0, std::min(pageCount() - 1, page + pages));
  if (target == page) return;
  {
    RenderLock lock(*this);
    page = target;
    if (selected >= 0) selected = page * perPage();
    loadPage();
  }
  requestUpdate();
}

void QuotesActivity::activate() {
  if (selected < 0) {
    cycleOrder();
    return;
  }
  const auto done = [this](const ActivityResult& result) { onChildDone(result); };
  if (showsBooks()) {
    const int row = selected - page * perPage();
    if (row < 0 || row >= rowCount || rows[row].path.empty()) return;
    startActivityForResult(makeUniqueNoThrow<QuotesActivity>(renderer, mappedInput, rows[row].path, insideReader),
                           done);
    return;
  }
  if (selected >= static_cast<int>(ids.size())) return;
  startActivityForResult(makeUniqueNoThrow<QuoteDetailActivity>(renderer, mappedInput, ids, selected, insideReader),
                         done);
}

// A screen opened from here closes with isCancelled when nothing changed, or with the
// quote the reader should reselect. Anything else means a quote was deleted or edited.
void QuotesActivity::onChildDone(const ActivityResult& result) {
  if (const auto* edit = std::get_if<QuoteEditResult>(&result.data)) {
    // Only the reader, which opened this screen, can reselect on the page: pass it on.
    setResult(ActivityResult(QuoteEditResult{edit->name}));
    finish();
    return;
  }
  if (result.isCancelled) return;
  storeChanged = true;
  RenderLock lock(*this);
  reload();
}

void QuotesActivity::leave() {
  ActivityResult result;
  result.isCancelled = !storeChanged;
  setResult(std::move(result));
  finish();
}

void QuotesActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    leave();
    return;
  }
  if (itemCount() <= 0) return;
  if (mappedInput.wasReleased(Button::Confirm)) {
    activate();
    return;
  }
  // A hold is caught while the button is still down, and swallows its own release, so it
  // is checked before the plain release that turns one page.
  if (mappedInput.wasLongPressed(Button::PageForward, HOLD_MS)) {
    flipPages(JUMP_PAGES);
    return;
  }
  if (mappedInput.wasLongPressed(Button::PageBack, HOLD_MS)) {
    flipPages(-JUMP_PAGES);
    return;
  }
  if (mappedInput.wasReleased(Button::PageForward)) {
    flipPages(1);
    return;
  }
  if (mappedInput.wasReleased(Button::PageBack)) {
    flipPages(-1);
    return;
  }
  // The front pair walks the rows; the side buttons above turn pages. Both are bound by
  // name, never through NavNext/NavPrevious, which fold a side button into the front one.
  cursorNavigator.onPressAndContinuous({Button::Right}, [this] { moveCursor(1); });
  cursorNavigator.onPressAndContinuous({Button::Left}, [this] { moveCursor(-1); });
}

void QuotesActivity::ensureWrapped(Block& block) const {
  if (block.wrapped) return;
  block.wrapped = true;
  const int width = renderer.getScreenWidth() - quotelist::RIGHT_INSET - quotelist::TEXT_X;
  if (!block.readable) {
    block.lines.assign(1, tr(STR_QUOTES_UNREADABLE));
    return;
  }
  // The closing mark rides on the last line, so every line leaves room for it.
  const int closeWidth = renderer.getTextWidth(BODY_FONT_ID, CLOSE_QUOTE);
  block.lines = renderer.wrappedText(BODY_FONT_ID, block.preview.c_str(), width - closeWidth, quotelist::MAX_BODY_LINES);
  if (block.lines.empty()) block.lines.emplace_back();
  tidyEllipsis(block.lines.back());
  if (!endsWithCloseQuote(block.lines.back())) block.lines.back() += CLOSE_QUOTE;
  block.preview.clear();
  block.preview.shrink_to_fit();
  block.title = renderer.truncatedText(SOURCE_TITLE_FONT_ID, block.title.c_str(), width, EpdFontFamily::BOLD);
  tidyEllipsis(block.title);
  block.place = renderer.truncatedText(SOURCE_FONT_ID, block.place.c_str(), width);
}

void QuotesActivity::drawTopRow(const char* label) const {
  const auto row = quotelist::sortRow(topRowMetrics());
  const bool marked = selected < 0;
  if (marked) {
    const int width = renderer.getTextWidth(TOP_ROW_FONT_ID, label);
    renderer.fillRect(row.x - TOP_ROW_MARK_PAD, row.y - quotelist::SORT_ROW_PAD, width + 2 * TOP_ROW_MARK_PAD,
                      row.height);
  }
  renderer.drawText(TOP_ROW_FONT_ID, row.x, row.y, label, !marked);
  renderer.drawLine(row.x, row.dividerY, row.x + row.width, row.dividerY);
}

void QuotesActivity::drawBlocks() const {
  const auto m = metrics();
  const int quoteWidth = renderer.getTextWidth(BODY_FONT_ID, OPEN_QUOTE);
  const bool twoSourceLines = !bookLevel();
  auto y = quotelist::contentTop(topRowMetrics());
  const int first = page * quotelist::BLOCKS_PER_PAGE;
  for (int i = 0; i < blockCount; ++i) {
    Block& entry = blocks[i];
    ensureWrapped(entry);
    char number[12];
    snprintf(number, sizeof(number), "%d", first + i + 1);
    const int numberWidth = renderer.getTextWidth(NUMBER_FONT_ID, number, EpdFontFamily::BOLD);
    const auto block = quotelist::place(m, y, static_cast<int>(entry.lines.size()), twoSourceLines && entry.readable,
                                        static_cast<int16_t>(numberWidth), static_cast<int16_t>(quoteWidth));
    // The number sits level with the quote's first line rather than at its top edge.
    const int boxY = block.numberBoxY + (m.bodyLineHeight - block.numberBoxHeight) / 2;
    const bool marked = first + i == selected;
    if (marked) renderer.fillRect(block.numberBoxX, boxY, block.numberBoxWidth, block.numberBoxHeight);
    renderer.drawText(NUMBER_FONT_ID, block.numberBoxX + (block.numberBoxWidth - numberWidth) / 2, boxY, number,
                      !marked, EpdFontFamily::BOLD);
    if (entry.readable) renderer.drawText(BODY_FONT_ID, block.hangingQuoteX, block.textY, OPEN_QUOTE);
    for (size_t line = 0; line < entry.lines.size(); ++line) {
      renderer.drawText(BODY_FONT_ID, block.textX, block.textY + static_cast<int>(line) * block.lineStep,
                        entry.lines[line].c_str());
    }
    if (entry.readable) {
      int sourceY = block.sourceY;
      if (twoSourceLines) {
        renderer.drawText(SOURCE_TITLE_FONT_ID, block.textX, sourceY, entry.title.c_str(), true, EpdFontFamily::BOLD);
        sourceY += block.sourceStep;
      }
      renderer.drawText(SOURCE_FONT_ID, block.textX, sourceY, entry.place.c_str());
    }
    if (i + 1 < blockCount) {
      renderer.drawLine(block.textX, block.dividerY, m.bandWidth - quotelist::RIGHT_INSET, block.dividerY);
    }
    y = static_cast<int16_t>(y + block.height);
  }
}

void QuotesActivity::drawBookRows() const {
  const auto m = metrics();
  auto y = quotelist::contentTop(topRowMetrics());
  const int first = page * perPage();
  for (int i = 0; i < rowCount; ++i) {
    const auto& entry = rows[i];
    const auto row = quotelist::bookRow(m, y);
    const bool marked = first + i == selected;
    // The mark covers the title and date lines only; the row's trailing gap stays white
    // so the next row does not look joined to it.
    if (marked) {
      renderer.fillRoundedRect(row.x, row.y, row.width, row.height - quotelist::DIVIDER_GAP / 2, BOOK_ROW_RADIUS,
                               Color::Black);
    }
    char count[12];
    snprintf(count, sizeof(count), "%d", entry.count);
    const int countWidth = renderer.getTextWidth(NUMBER_FONT_ID, count, EpdFontFamily::BOLD);
    renderer.drawText(NUMBER_FONT_ID, row.countRightX - countWidth, row.countY, count, !marked, EpdFontFamily::BOLD);
    const int titleRoom = row.countRightX - countWidth - quotelist::BOOK_ROW_PAD - row.titleX;
    auto title = renderer.truncatedText(NUMBER_FONT_ID, entry.title.c_str(), titleRoom);
    tidyEllipsis(title);
    renderer.drawText(NUMBER_FONT_ID, row.titleX, row.titleY, title.c_str(), !marked);
    renderer.drawText(SOURCE_FONT_ID, row.dateX, row.dateY, entry.latest.c_str(), !marked);
    y = static_cast<int16_t>(y + row.height);
  }
}

std::string QuotesActivity::footer() const {
  char text[64];
  if (pageCount() > 1)
    snprintf(text, sizeof(text), tr(STR_QUOTES_PAGE_OF), page + 1, pageCount());
  else if (showsBooks())
    snprintf(text, sizeof(text), tr(STR_QUOTES_COUNT_LIBRARY), bookTotal, quoteTotal);
  else
    snprintf(text, sizeof(text), tr(STR_QUOTES_COUNT_ONE_PAGE), quoteTotal, bookTotal);
  return text;
}

void QuotesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  // Inside one book the header names it, bold, after the screen's own name (mockup B2).
  if (bookLevel() && !bookTitle.empty()) {
    // The header drops "Trích dẫn/" when the title alone fills the row, so a long title is
    // cut here first, to the room left after the screen's own name.
    const std::string prefix = std::string(tr(STR_QUOTES)) + "/";
    const int room = renderer.getScreenWidth() - 2 * HEADER_SIDE -
                     renderer.getTextWidth(UI_12_FONT_ID, prefix.c_str(), EpdFontFamily::REGULAR,
                                           BidiUtils::BidiBaseDir::AUTO, 1) - 2;
    auto title = renderer.truncatedText(UI_12_FONT_ID, bookTitle.c_str(), room, EpdFontFamily::BOLD, 1);
    tidyEllipsis(title);
    if (tenorchrome::enabled())
      tenorchrome::drawHeader(renderer, title.c_str(), tr(STR_QUOTES));
    else
      drawNavigationHeader((prefix + title).c_str());
  } else {
    drawNavigationHeader(tr(STR_QUOTES));
  }

  if (itemCount() <= 0) {
    const auto m = metrics();
    const char* label = tr(STR_QUOTES_EMPTY);
    const int width = renderer.getTextWidth(UI_12_FONT_ID, label);
    renderer.drawText(UI_12_FONT_ID, (m.bandWidth - width) / 2, m.bandTop + (m.bandBottom - m.bandTop) / 3, label);
  } else {
    static constexpr StrId labels[] = {StrId::STR_QUOTES_VIEW_BOOKS,  StrId::STR_QUOTES_VIEW_NEWEST,
                                       StrId::STR_QUOTES_VIEW_OLDEST, StrId::STR_QUOTES_SORT_NEWEST,
                                       StrId::STR_QUOTES_SORT_OLDEST, StrId::STR_QUOTES_SORT_PAGE};
    const char* label = I18N.get(labels[static_cast<uint8_t>(order)]);
    const auto footerText = footer();
    // One prewarm scope over the whole page, the pattern the reader uses: the scan pass
    // records every codepoint, the second pass draws them from the warmed cache.
    auto* fcm = renderer.getFontCacheManager();
    auto scope = fcm->createPrewarmScope();
    for (int pass = 0; pass < 2; ++pass) {
      if (pass == 1) scope.endScanAndPrewarm();
      drawTopRow(label);
      if (showsBooks())
        drawBookRows();
      else
        drawBlocks();
      tenorchrome::drawTip(renderer, footerText.c_str());
    }
  }

  // An empty store leaves Back as the only button that does anything.
  const bool any = itemCount() > 0;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), any ? tr(STR_SELECT) : "", any ? tr(STR_DIR_UP) : "",
                                            any ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
