#include "QuoteDetailActivity.h"
#include "X3FileReaderChrome.h"

#include <Epub.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#ifdef SIMULATOR
#include <cstdlib>
#endif

#include "QuoteTrimActivity.h"
#include "components/QuoteMarkGlyph.h"
#include "components/TenorMenuChrome.h"
#include "components/PageScrollbar.h"
#include "components/TouchScroll.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglyScrap.h"

namespace quotetext {
std::string clipped(const std::string& text, const size_t limit) {
  if (text.size() <= limit) return text;
  size_t length = limit;
  while (length && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) --length;
  return text.substr(0, length);
}

void tidyEllipsis(std::string& line) {
  static constexpr char SPACED[] = " \xe2\x80\xa6";
  if (line.size() >= 4 && line.compare(line.size() - 4, 4, SPACED) == 0) line.erase(line.size() - 4, 1);
}

bool endsWithCloseQuote(const std::string& text) {
  return text.size() >= 3 && text.compare(text.size() - 3, 3, "\xe2\x80\x9d") == 0;
}
}  // namespace quotetext

using quotetext::clipped;
using quotetext::endsWithCloseQuote;
using quotetext::tidyEllipsis;

namespace {

// Mockup D2's faces, all in flash: the title in Noto Serif 12 italic, the three lines under
// it in the UI subtitle tier, the position in the header in the UI body tier.
constexpr int TITLE_FONT_ID = NOTOSERIF_12_FONT_ID;
constexpr int META_FONT_ID = UI_10_FONT_ID;
constexpr int POSITION_FONT_ID = UI_12_FONT_ID;
// The delete confirmation quotes the words in the list's own face (mockup M2).
constexpr int EXCERPT_FONT_ID = NOTOSERIF_14_FONT_ID;
constexpr int NOTE_FONT_ID = UI_10_FONT_ID;

// A book title of 170 characters exists on a real card: two lines, then an ellipsis.
constexpr int UGLY_META_STEP = 32;  // the pen's line pitch for the facts under the quote, with air between the lines
constexpr int TITLE_MAX_LINES = 2;
constexpr int EXCERPT_MAX_LINES = 3;
constexpr int EXCERPT_MARGIN = 44;
constexpr int EXCERPT_GAP = 16;
constexpr int NOTE_MAX_LINES = 2;
// Top of the words on the delete confirmation: high enough that three lines of them and the
// note clear the centred dialog.
constexpr int DELETE_TOP = 96;
// 1024 bytes wrap to about thirty lines at 16; the cap only guards the vector.
constexpr int BODY_MAX_LINES = 96;
// Header bottom to the top of the big opening mark (mockup D2).
constexpr int GLYPH_TOP_GAP = 56;
// Kept clear above the button hints, so the last line of a full page does not sit on them.
constexpr int HINTS_CLEARANCE = 16;
constexpr int CHEVRON_CLEARANCE = 6;
constexpr unsigned long FAILURE_MS = 1500;

constexpr char OPEN_QUOTE[] = "\xe2\x80\x9c";
// The 8 lines of abuse a quote can earn at the head of its page under tenor/ugly (ugly::quotepage::quipIndex picks one).
constexpr StrId QUIP_LINES[ugly::quotepage::QUIPS] = {
    StrId::STR_UGLY_QUOTE_QUIP_1, StrId::STR_UGLY_QUOTE_QUIP_2, StrId::STR_UGLY_QUOTE_QUIP_3, StrId::STR_UGLY_QUOTE_QUIP_4,
    StrId::STR_UGLY_QUOTE_QUIP_5, StrId::STR_UGLY_QUOTE_QUIP_6, StrId::STR_UGLY_QUOTE_QUIP_7, StrId::STR_UGLY_QUOTE_QUIP_8};
constexpr char CLOSE_QUOTE[] = "\xe2\x80\x9d";

// The chapter by number and, when the book's own table of contents names it, by name. The
// lookup is the one the reader's status bar makes: spine item -> table of contents entry.
// `book` is the loaded book the reader passed down, used when the quote is one of its own;
// otherwise the book's cached metadata is loaded for the lookup. A book with no cached table
// of contents leaves the number standing alone.
std::string chapterLine(const QuoteRecord& quote, const Epub* book) {
  char line[192];
  std::unique_ptr<Epub> loaded;
  if (!book || book->getPath() != quote.path) {
    loaded = makeUniqueNoThrow<Epub>(quote.path, "/.crosspoint");
    book = loaded && loaded->load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true) ? loaded.get() : nullptr;
  }
  if (book) {
    const int tocIndex = book->getTocIndexForSpineIndex(quote.spine);
    if (tocIndex >= 0) {
      const std::string title = book->getTocItem(tocIndex).title;
      if (!title.empty()) {
        snprintf(line, sizeof(line), tr(STR_QUOTES_DETAIL_CHAPTER), quote.spine + 1, clipped(title, 120).c_str());
        return line;
      }
    }
  }
  snprintf(line, sizeof(line), tr(STR_QUOTES_DETAIL_CHAPTER_ONLY), quote.spine + 1);
  return line;
}

// The moment the quote was kept. Records written before the clock stamp existed carry no
// minute and show their date alone.
std::string whenLine(const QuoteRecord& quote) {
  char line[64];
  const unsigned day = quote.day % 100, month = quote.day / 100 % 100, year = quote.day / 10000;
  if (quote.minute < 1440)
    snprintf(line, sizeof(line), tr(STR_QUOTES_DETAIL_WHEN), day, month, year, quote.minute / 60u, quote.minute % 60u);
  else
    snprintf(line, sizeof(line), tr(STR_QUOTES_DETAIL_DAY), day, month, year);
  return line;
}

// Plots the big opening mark pixel by pixel through drawPixel, which applies the screen
// orientation. The bytes are packed for drawImage's blit (components/QuoteMarkGlyph.h),
// but that blit snaps its x to a whole byte and drew the mark as stripes on the
// simulator. This is the mapping drawIcon uses for the same packing: stored (row, column)
// lands at (x + rows - 1 - row, y + column).
void drawQuoteMark(const GfxRenderer& renderer, const int x, const int y) {
  constexpr int stride = (QUOTE_MARK_DRAW_WIDTH + 7) / 8;
  for (int row = 0; row < QUOTE_MARK_DRAW_HEIGHT; ++row) {
    for (int column = 0; column < QUOTE_MARK_DRAW_WIDTH; ++column) {
      const uint8_t byte = kQuoteMarkGlyphBitmap[row * stride + (column >> 3)];
      if (((byte >> (7 - (column & 7))) & 1) == 0) renderer.drawPixel(x + QUOTE_MARK_DRAW_HEIGHT - 1 - row, y + column);
    }
  }
}

int headerTextY(const GfxRenderer& renderer, const int fontId) {
  if (tenorchrome::enabled())
    return tenorchrome::HEADER_TOP + (tenorchrome::headerHeight() - renderer.getLineHeight(fontId)) / 2;
  const auto& m = UITheme::getInstance().getMetrics();
  return m.topPadding + (m.headerHeight - renderer.getLineHeight(fontId)) / 2;
}

}  // namespace

int quotesHeaderBottom() {
  if (tenorchrome::enabled()) return tenorchrome::tabTop();
  const auto& m = UITheme::getInstance().getMetrics();
  return m.topPadding + m.headerHeight;
}

void QuoteDetailActivity::onEnter() {
  Activity::onEnter();
  if (ids.empty()) {
    leave();
    return;
  }
  {
    RenderLock lock(*this);
    show(std::max(0, std::min(index, static_cast<int>(ids.size()) - 1)));
  }
  logShown();
  if (start == Start::Edit && loaded) openMenu(Menu::Edit);
  else if (start == Start::Delete) openMenu(Menu::Delete);
  requestUpdate();
}

quotedetail::Metrics QuoteDetailActivity::metrics() const {
  quotedetail::Metrics m;
  m.bandX = 0;
  m.bandWidth = static_cast<int16_t>(renderer.getScreenWidth());
  m.bandTop = static_cast<int16_t>(quotesHeaderBottom() + GLYPH_TOP_GAP);
  // A title on two lines pushes the three meta lines down by one line; the band gives that
  // line back so the page line still clears the button hints.
  const int extraTitle =
      titleLines.size() > 1 ? static_cast<int>(titleLines.size() - 1) * renderer.getLineHeight(TITLE_FONT_ID) : 0;
  // A page with more of the quote after it shows the "more below" chevron over the hints.
  const int bottom = std::min(renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight -
                                  HINTS_CLEARANCE,
                              tenorchrome::moreBelowChevronTopY(renderer) - CHEVRON_CLEARANCE);
  m.bandBottom = static_cast<int16_t>(bottom - extraTitle);
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const auto body = screenLayout().bodyRect;
    m.bandTop = static_cast<int16_t>(body.y + renderer.getLineHeight(SMALL_FONT_ID) + 8);
    m.bandBottom = static_cast<int16_t>(body.bottom());
  }
  m.glyphWidth = QUOTE_MARK_GLYPH_WIDTH;
  m.glyphHeight = QUOTE_MARK_GLYPH_HEIGHT;
  m.lineHeight18 = static_cast<int16_t>(renderer.getLineHeight(NOTOSERIF_18_FONT_ID));
  m.lineHeight16 = static_cast<int16_t>(renderer.getLineHeight(NOTOSERIF_16_FONT_ID));
  return m;
}

void QuoteDetailActivity::show(const int newIndex) {
  index = newIndex;
  quotePage = 0;
  pages = 1;
  bodyPages = 1;
  metaOnLast = 0;
  titleLines.clear();
  chapter.clear();
  when.clear();
  pageLine.clear();
  // load() leaves `quote` as it was when the record cannot be read, which would carry the
  // previous quote's words into the delete confirmation.
  quote = QuoteRecord{};
  loaded = quotes::load(ids[index], quote);
  auto m = metrics();
  const int width = quotedetail::bodyX1(m) - quotedetail::bodyX0(m);
  if (!loaded) {
    size18 = false;
    lines.assign(1, tr(STR_QUOTES_UNREADABLE));
    linesPerPage = 1;
    return;
  }
  if (!quote.title.empty()) {
    titleLines = renderer.wrappedText(TITLE_FONT_ID, clipped(quote.title, 512).c_str(), width, TITLE_MAX_LINES,
                                      EpdFontFamily::ITALIC);
    m = metrics();
  }
  chapter = chapterFor(width);
  if (!titleLines.empty()) tidyEllipsis(titleLines.back());
  when = whenLine(quote);
  char page[48];
  snprintf(page, sizeof(page), tr(STR_QUOTES_DETAIL_PAGE), quote.page + 1);
  pageLine = page;

  if (penPage()) {
    titleLines.clear();  // the facts are set line by line from the quote's own title, not from these
    layoutUglyBody();
    layoutX3Pages();
    return;
  }
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const int step = metaStep();
    const int metaCount = visitX3Meta(false);
    const auto body = endsWithCloseQuote(quote.text) ? quote.text : quote.text + CLOSE_QUOTE;
    auto largeLines = renderer.wrappedText(NOTOSERIF_18_FONT_ID, body.c_str(), width, BODY_MAX_LINES);
    size18 = quotedetail::bodyTop(m) + static_cast<int>(largeLines.size()) * m.lineHeight18 + 8 + metaCount * step <=
             m.bandBottom;
    lines = size18 ? std::move(largeLines)
                   : renderer.wrappedText(NOTOSERIF_16_FONT_ID, body.c_str(), width, BODY_MAX_LINES);
    layoutX3Pages();
    return;
  }

  // The opening mark is the glyph above the words; the closing one rides on the last word.
  const std::string body = endsWithCloseQuote(quote.text) ? quote.text : quote.text + CLOSE_QUOTE;
  auto lines18 = renderer.wrappedText(NOTOSERIF_18_FONT_ID, body.c_str(), width, BODY_MAX_LINES);
  auto lines16 = renderer.wrappedText(NOTOSERIF_16_FONT_ID, body.c_str(), width, BODY_MAX_LINES);
  const auto fontChoice =
      quotedetail::chooseFont(static_cast<int>(lines18.size()), static_cast<int>(lines16.size()), m);
  size18 = fontChoice.useSize18;
  pages = fontChoice.pages;
  lines = std::move(size18 ? lines18 : lines16);
  linesPerPage = std::max(1, quotedetail::linesPerPage(size18 ? m.lineHeight18 : m.lineHeight16, m));
}

std::string QuoteDetailActivity::chapterFor(const int width) {
  const auto id = ids[index];
  for (const auto& known : chapters)
    if (known.first == id) return known.second;
  std::string line = chapterLine(quote, openBook.get());
  if (!tenorchrome::x3ScreenLayoutEnabled()) {
    line = renderer.truncatedText(META_FONT_ID, line.c_str(), width);
    tidyEllipsis(line);
  }
  if (chapters.size() >= CHAPTER_CACHE) chapters.erase(chapters.begin());
  chapters.emplace_back(id, line);
  return line;
}

// For the simulator journeys (test/reading_stats_simulator/test_quotes_v1011.py); device
// builds carry none of it.
void QuoteDetailActivity::logShown() const {
#ifdef SIMULATOR
  if (ids.empty()) return;
  LOG_DBG("QTS", "Quote detail %d/%d %016llx: Noto Serif %d, page %d/%d", index + 1, static_cast<int>(ids.size()),
          static_cast<unsigned long long>(ids[index]), size18 ? 18 : 16, quotePage + 1, pages);
#endif
}

void QuoteDetailActivity::openMenu(const Menu which) {
  const auto pick = [this](const int option) { choice = option; };
  {
    RenderLock lock(*this);
    menu = which;
    // The header above already shows the quote's place in the list, so the options are
    // titled by the screen's name alone.
    if (which == Menu::Options) {
      // A record that cannot be read has no words to trim or reselect; deleting it is all
      // there is to do.
      static constexpr StrId options[] = {StrId::STR_QUOTES_MENU_EDIT, StrId::STR_QUOTES_MENU_DELETE,
                                          StrId::STR_CANCEL};
      if (loaded)
        popup.show(StrId::STR_QUOTES, options, 3, -1, pick);
      else
        popup.show(StrId::STR_QUOTES, options + 1, 2, -1, pick);
    } else if (which == Menu::Edit) {
      static constexpr StrId options[] = {StrId::STR_QUOTES_EDIT_TRIM, StrId::STR_QUOTES_EDIT_RESELECT,
                                          StrId::STR_CANCEL};
      const auto parentFrame = popup.lastFrame();
      if (tenorchrome::kTouchShell && !parentFrame.empty())
        popup.showInFrame(parentFrame, StrId::STR_QUOTES_MENU_EDIT, options, 3, -1, pick);
      else
        popup.show(StrId::STR_QUOTES_MENU_EDIT, options, 3, -1, pick);
    } else if (which == Menu::Delete) {
      // Mockup M2: the words being deleted, in quotation marks, above the system dialog,
      // which opens on Cancel. The dialog's title is one bold line, which cut the note
      // short, so the note stands under the words and the question titles the dialog.
      confirmingDelete = true;
      const int width = renderer.getScreenWidth() - 2 * EXCERPT_MARGIN;
      if (loaded) {
        const std::string words = std::string(OPEN_QUOTE) + clipped(quote.text, 400);
        const int closeWidth = renderer.getTextWidth(EXCERPT_FONT_ID, CLOSE_QUOTE);
        excerpt = renderer.wrappedText(EXCERPT_FONT_ID, words.c_str(), width - closeWidth, EXCERPT_MAX_LINES);
        if (excerpt.empty()) excerpt.emplace_back(OPEN_QUOTE);
        tidyEllipsis(excerpt.back());
        if (!endsWithCloseQuote(excerpt.back())) excerpt.back() += CLOSE_QUOTE;
      } else {
        // No words to quote: the line the detail itself shows for such a record, unquoted.
        excerpt = renderer.wrappedText(EXCERPT_FONT_ID, tr(STR_QUOTES_UNREADABLE), width, EXCERPT_MAX_LINES);
        if (excerpt.empty()) excerpt.emplace_back();
      }
      note = renderer.wrappedText(NOTE_FONT_ID, tr(STR_QUOTES_DELETE_NOTE), width, NOTE_MAX_LINES);
      static constexpr StrId options[] = {StrId::STR_CANCEL, StrId::STR_CONFIRM};
      popup.show(StrId::STR_QUOTES_DELETE_HEADING, options, 2, -1, pick);
#ifdef SIMULATOR
      LOG_DBG("QTS", "Quote delete asks %016llx: %s", static_cast<unsigned long long>(ids[index]),
              excerpt.front().c_str());
#endif
    }
  }
  requestUpdate();
}

// Runs after the popup has closed, never from inside its callback: the next menu reuses
// the same popup, and replacing its callback while that callback runs would destroy it.
void QuoteDetailActivity::chooseFromMenu(const Menu which, int option) {
  switch (which) {
    case Menu::Options:
      // Without the Edit row the options start at Delete.
      if (!loaded && option >= 0) option++;
      if (option == 0) {
        if (insideReader)
          openMenu(Menu::Edit);
        else
          openTrim();
      } else if (option == 1) {
        openMenu(Menu::Delete);
      }
      break;
    case Menu::Edit:
      if (option == 0) {
        openTrim();
      } else if (option == 1) {
        // The reader that opened the list jumps to the quote and reselects it.
        setResult(ActivityResult(QuoteEditResult{quotes::nameOf(ids[index])}));
        finish();
      }
      break;
    case Menu::Delete: {
      {
        RenderLock lock(*this);
        confirmingDelete = false;
        excerpt.clear();
      }
      if (option == 1) deleteCurrent();
      break;
    }
    case Menu::None:
    default:
      break;
  }
  requestUpdate();
}

void QuoteDetailActivity::openTrim() {
  startActivityForResult(makeUniqueNoThrow<QuoteTrimActivity>(renderer, mappedInput, ids[index]),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           changed = true;
                           {
                             RenderLock lock(*this);
                             show(index);
                           }
                           logShown();
                         });
}

// After a delete the quote that took its place is shown: the next one at the same
// position, or the one before when it was the last. None left closes the screen.
void QuoteDetailActivity::deleteCurrent() {
  const auto id = ids[index];
  if (!quotes::remove(id)) {
    LOG_ERR("QTS", "Quote delete failed %016llx", static_cast<unsigned long long>(id));
    failure = tr(STR_QUOTES_DELETE_FAILED);
    failureAt = millis();
    return;
  }
  LOG_INF("QTS", "Quote deleted %016llx", static_cast<unsigned long long>(id));
  changed = true;
  {
    RenderLock lock(*this);
    ids.erase(ids.begin() + index);
    if (!ids.empty()) show(std::min(index, static_cast<int>(ids.size()) - 1));
  }
  if (ids.empty()) {
    leave();
    return;
  }
  logShown();
}

void QuoteDetailActivity::leave() {
  ActivityResult result;
  result.isCancelled = !changed;
  setResult(std::move(result));
  finish();
}

void QuoteDetailActivity::loop() {
  using Button = MappedInputManager::Button;
  if (failure) {
    if (millis() - failureAt >= FAILURE_MS) {
      failure = nullptr;
      requestUpdate();
    }
    return;
  }
  if (popup.isActive()) {
    choice = -1;
    popup.handleInput(mappedInput, [this] { requestUpdate(); });
    if (!popup.isActive()) {
      const Menu which = menu;
      menu = Menu::None;
      chooseFromMenu(which, choice);
    }
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    leave();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    openMenu(Menu::Options);
    return;
  }
  // Touch: the bar's pill or a hold on the quote opens its options; a sideways swipe goes to the quote before
  // or after, an up or down swipe turns the pages of a long one.
  if (tenorchrome::kTouchShell) {
    int x = 0, y = 0, dy = 0;
    unsigned long heldMs = 0;
    if ((mappedInput.wasScreenTapped(x, y) && tenorchrome::footActionAt(x, y) == 0) ||
        mappedInput.wasScreenLongPress(x, y)) {
      openMenu(Menu::Options);
      return;
    }
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Right) {
      stepQuote(swipe == MappedInputManager::SwipeDir::Left ? 1 : -1);
      return;
    }
    if (mappedInput.wasVerticalSwipe(dy, heldMs)) {
      const auto m = metrics();
      stepPage(touchscroll::rows(dy, heldMs, m.bandBottom - quotedetail::bodyTop(m), 1));
      return;
    }
  }
  // Front pair: the quote before or after, in the list's order, stopping at both ends.
  const bool next = mappedInput.wasReleased(Button::Right);
  if (next || mappedInput.wasReleased(Button::Left)) {
    stepQuote(next ? 1 : -1);
    return;
  }
  // Side buttons: the pages of a quote too long for one screen.
  const bool forward = mappedInput.wasReleased(Button::PageForward);
  if (forward || mappedInput.wasReleased(Button::PageBack)) stepPage(forward ? 1 : -1);
}

void QuoteDetailActivity::stepQuote(const int direction) {
  const int target = index + direction;
  if (target < 0 || target >= static_cast<int>(ids.size())) return;
  {
    RenderLock lock(*this);
    show(target);
  }
  logShown();
  requestUpdate();
}

void QuoteDetailActivity::stepPage(const int direction) {
  const int target = std::max(0, std::min(pages - 1, quotePage + direction));
  if (target == quotePage) return;
  quotePage = target;
  logShown();
  requestUpdate();
}

void QuoteDetailActivity::drawQuote() {
  const bool x3 = tenorchrome::x3ScreenLayoutEnabled();
  if (x3) {
    auto* cache = renderer.getFontCacheManager();
    auto scope = cache->createPrewarmScope();
    for (int pass = 0; pass < 2; ++pass) {
      if (pass == 1) scope.endScanAndPrewarm();
      drawX3Content();
    }
    return;
  }
  drawNavigationHeader(tr(STR_QUOTES));
  const auto m = metrics();
  char position[24];
  snprintf(position, sizeof(position), "%d/%d", index + 1, static_cast<int>(ids.size()));
  // tenor/ugly on the button readers: the count and the facts under the quote written by hand; the quoted words keep
  // their book font.
  const bool byHand = shell::uglyParts();
  const auto hand = [this](const int fontId, const int x, const int y, const char* text, const ugly::Size size) {
    return ugly::text(renderer, size, x, y + renderer.getFontAscenderSize(fontId), text);
  };
  // Touch: the strip at the top holds the clock and the battery there; the position goes to its middle note.
  if (tenorchrome::kTouchShell)
    tenorchrome::noteStatus(position);
  else if (byHand)
    hand(POSITION_FONT_ID, quotedetail::headerPositionRight(m) - ugly::width(renderer, ugly::Size::S22, position),
         headerTextY(renderer, POSITION_FONT_ID), position, ugly::Size::S22);
  else
    renderer.drawText(POSITION_FONT_ID,
                      quotedetail::headerPositionRight(m) - renderer.getTextWidth(POSITION_FONT_ID, position),
                      headerTextY(renderer, POSITION_FONT_ID), position);

  if (penPage() && loaded) {
    // tenor/ugly on the touch reader: the quote, the facts and the scribbles in the pen under the header, which stays the screen's
    auto* penCache = renderer.getFontCacheManager();
    auto penScope = penCache->createPrewarmScope();
    for (int pass = 0; pass < 2; ++pass) {
      if (pass == 1) penScope.endScanAndPrewarm();
      drawUglyPage();
    }
    return;
  }
  const int font = size18 ? NOTOSERIF_18_FONT_ID : NOTOSERIF_16_FONT_ID;
  const int lineHeight = size18 ? m.lineHeight18 : m.lineHeight16;
  const int x = quotedetail::bodyX0(m);
  const bool lastPage = quotePage + 1 >= pages;
  const int first = quotePage * linesPerPage;
  const int end = lastPage ? static_cast<int>(lines.size())
                           : std::min(static_cast<int>(lines.size()), first + linesPerPage);
  const int metaWidth = quotedetail::bodyX1(m) - x;

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  for (int pass = 0; pass < 2; ++pass) {
    if (pass == 1) scope.endScanAndPrewarm();
    if (quotePage == 0 && loaded) {
      drawQuoteMark(renderer, quotedetail::glyphX(m), quotedetail::glyphY(m));
    }
    int y = quotedetail::bodyTop(m);
    for (int i = first; i < end; ++i) {
      renderer.drawText(font, x, y, lines[i].c_str());
      y += lineHeight;
    }
    if (!lastPage) tenorchrome::drawMoreBelowChevron(renderer);
    if (!lastPage || !loaded) continue;
    const auto meta = quotedetail::metaBlock(m, static_cast<int16_t>(y));
    const int titleStep = renderer.getLineHeight(TITLE_FONT_ID);
    if (byHand) {
      ugly::line(renderer, meta.x, meta.ruleY, meta.x + quotedetail::META_RULE_WIDTH, meta.ruleY - 1, 890, 2);
      for (size_t i = 0; i < titleLines.size(); ++i)
        hand(TITLE_FONT_ID, meta.x, meta.titleY + static_cast<int>(i) * titleStep, titleLines[i].c_str(), ugly::Size::S30);
      const int extra = titleLines.size() > 1 ? static_cast<int>(titleLines.size() - 1) * titleStep : 0;
      hand(META_FONT_ID, meta.x, meta.chapterY + extra, chapter.c_str(), ugly::Size::S22);
      hand(META_FONT_ID, meta.x, meta.whenY + extra, when.c_str(), ugly::Size::S22);
      hand(META_FONT_ID, meta.x, meta.pageY + extra, ugly::fit(renderer, ugly::Size::S22, pageLine, metaWidth).c_str(), ugly::Size::S22);
      continue;
    }
    renderer.fillRect(meta.x, meta.ruleY, quotedetail::META_RULE_WIDTH, quotedetail::META_RULE_HEIGHT);
    for (size_t i = 0; i < titleLines.size(); ++i) {
      renderer.drawText(TITLE_FONT_ID, meta.x, meta.titleY + static_cast<int>(i) * titleStep, titleLines[i].c_str(),
                        true, EpdFontFamily::ITALIC);
    }
    const int extra = titleLines.size() > 1 ? static_cast<int>(titleLines.size() - 1) * titleStep : 0;
    renderer.drawText(META_FONT_ID, meta.x, meta.chapterY + extra, chapter.c_str());
    renderer.drawText(META_FONT_ID, meta.x, meta.whenY + extra, when.c_str());
    renderer.drawText(META_FONT_ID, meta.x, meta.pageY + extra,
                      renderer.truncatedText(META_FONT_ID, pageLine.c_str(), metaWidth).c_str());
  }
}

void QuoteDetailActivity::drawDeleteConfirmation() const {
  // The quoted words and the note sit in the upper part of the screen so the centred
  // dialog, which asks the question, leaves them readable.
  int y = DELETE_TOP;
  for (const auto& line : excerpt) {
    renderer.drawCenteredText(EXCERPT_FONT_ID, y, line.c_str());
    y += renderer.getLineHeight(EXCERPT_FONT_ID);
  }
  y += EXCERPT_GAP;
  for (const auto& line : note) {
    renderer.drawCenteredText(NOTE_FONT_ID, y, line.c_str());
    y += renderer.getLineHeight(NOTE_FONT_ID);
  }
}

void QuoteDetailActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (confirmingDelete)
    drawDeleteConfirmation();
  else
    drawQuote();
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    char position[48];
    positionLabel(position, sizeof(position));
    x3readerchrome::drawPath(renderer, screenLayout().barRect, icon_tenor_reader_tools_40, tr(STR_QUOTES),
                             confirmingDelete ? nullptr : position);
  }
  // Touch: the options (Edit, Delete) are a pill in the bar, as Select is on the buttons.
  if (tenorchrome::kTouchShell) tenorchrome::noteFootActions(confirmingDelete ? nullptr : tr(STR_QUOTES_OPTIONS));
  // The popup draws its own button hints over the screen under it.
  if (popup.processRender(renderer, mappedInput)) return;
  // Each hint names what its button does now: at either end of the list the arrow that
  // would leave it has nothing to do, so it is not shown.
  const bool hasPrevious = index > 0, hasNext = index + 1 < static_cast<int>(ids.size());
  // The pen's foot bar has room for 3 letters of a label, so under tenor/ugly the Select key shows its tick; the options
  // it opens are named in the menu that comes up.
  const char* const confirm = shell::uglyParts() ? tr(STR_SELECT) : tr(STR_QUOTES_OPTIONS);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, hasPrevious ? tr(STR_DIR_LEFT) : "",
                                            hasNext ? tr(STR_DIR_RIGHT) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (tenorchrome::kTouchShell && !confirmingDelete) {
    const auto area = metrics();
    const int top = quotedetail::bodyTop(area);
    const int height = area.bandBottom - top;
    auto target = makeUiTarget(renderer);
    PageScrollbar::instance().draw(renderer, target, top, area.bandBottom, pages * height, quotePage * height, this);
  }
#ifdef UGLY_FRAME_LOG
  if (shell::uglyParts()) LOG_INF("UGLY", "QuoteDetail frame quote=%d page=%d", index, quotePage);
#endif
  if (failure) {
    // drawPopup lays its box over the frame and refreshes the panel itself.
    GUI.drawPopup(renderer, failure);
    return;
  }
  renderer.displayBuffer();
}

tenorchrome::X3ScreenLayout QuoteDetailActivity::screenLayout() const { return tenorchrome::x3ScreenLayout(renderer); }

// tenor/ugly writes the facts at its own line pitch, the one its paragraphs use.
int QuoteDetailActivity::metaStep() const {
  return penPage() ? UGLY_META_STEP : renderer.getLineHeight(SMALL_FONT_ID);
}

// The quote's place in the list, and its page only when it runs over more than one.
void QuoteDetailActivity::positionLabel(char* out, const size_t size) const {
  if (pages > 1) snprintf(out, size, "%d/%d, %d/%d", index + 1, static_cast<int>(ids.size()), quotePage + 1, pages);
  else snprintf(out, size, "%d/%d", index + 1, static_cast<int>(ids.size()));
}

int QuoteDetailActivity::visitX3Meta(const bool paint, const int firstLine, const int top) const {
  const auto body = bodyArea();
  const int step = metaStep();
  const bool hand = penPage();
  const auto measure = [&](const char* line) {
    return hand ? ugly::width(renderer, ugly::Size::S22, line) : renderer.getTextWidth(SMALL_FONT_ID, line);
  };
  int count = 0;
  for (const char* text : {quote.title.c_str(), chapter.c_str(), when.c_str(), pageLine.c_str()}) {
    count += x3readerchrome::eachInfoLine(
        renderer, text,
        [&](const int offset, const char* line) {
          const int row = count + offset - firstLine;
          const int lineTop = top + row * step;
          if (!paint || row < 0 || lineTop + step > body.bottom()) return;
          if (!hand) {
            tenorchrome::drawInfoInRect(renderer, line, {body.x, lineTop, body.width, step}, 1);
            return;
          }
          // centred on the ink, like the lines of tenor/cross
#ifdef UGLY_FRAME_LOG
          if (!renderer.isFontCacheScanning()) LOG_INF("UGLY", "QuoteMeta page=%d row=%d y=%d %s", quotePage, row, lineTop, line);
#endif
          const auto ink = ugly::textBounds(renderer, ugly::Size::S22, line);
          ugly::text(renderer, ugly::Size::S22, body.x + (body.width - (ink.x0 + ink.x1)) / 2,
                     lineTop + ugly::ascent(ugly::Size::S22), line);
        },
        measure);
  }
  return count;
}

void QuoteDetailActivity::layoutX3Pages() {
  const auto area = metrics();
  const int step = metaStep();
  if (penPage()) {
    using namespace ugly::quotepage;
    const auto body = bodyArea();
    const int rowsTop = body.y + BODY_BASE - BODY_ASCENT;
    linesPerPage = capacityFor(body.y, body.bottom());
    bodyPages = uglyMap.pages;
    metaOnLast = std::max(0, (body.bottom() - (rowsTop + uglyMap.lastLines * ROW_PITCH + META_GAP)) / step);
    metaPerPage = std::max(1, (body.bottom() - rowsTop) / step);
    pages = bodyPages + (std::max(0, visitX3Meta(false) - metaOnLast) + metaPerPage - 1) / metaPerPage;
    return;
  }
  const int lineHeight = size18 ? area.lineHeight18 : area.lineHeight16;
  linesPerPage = std::max(1, quotedetail::linesPerPage(lineHeight, area));
  bodyPages = std::max(1, (static_cast<int>(lines.size()) + linesPerPage - 1) / linesPerPage);
  const int lastLines = static_cast<int>(lines.size()) - (bodyPages - 1) * linesPerPage;
  const int metaTop = quotedetail::bodyTop(area) + lastLines * lineHeight + 8;
  metaOnLast = std::max(0, (area.bandBottom - metaTop) / step);
  metaPerPage = std::max(1, (area.bandBottom - quotedetail::bodyTop(area)) / step);
  pages = bodyPages + (std::max(0, visitX3Meta(false) - metaOnLast) + metaPerPage - 1) / metaPerPage;
}

void QuoteDetailActivity::drawX3Content() const {
  const auto body = screenLayout().bodyRect;
  if (!penPage()) {
    char position[48];
    positionLabel(position, sizeof(position));
    // Centred on its ink: a short count such as "1/3" sat a pixel off the centre by its advance width.
    const auto ink = renderer.getTextInkBounds(SMALL_FONT_ID, position, EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, body.x + (body.width - (ink[2] - ink[0])) / 2 - ink[0], body.y, position);
  }
  const auto area = metrics();
  const int top = quotedetail::bodyTop(area);
  if (!loaded) {
    tenorchrome::drawInfoInRect(renderer, tr(STR_QUOTES_UNREADABLE), {body.x, top, body.width, body.bottom() - top});
    return;
  }
  if (penPage()) {
    drawUglyPage();
    return;
  }
  if (quotePage < bodyPages) {
    const int first = quotePage * linesPerPage;
    const int end = std::min(static_cast<int>(lines.size()), first + linesPerPage);
    if (quotePage == 0 && loaded) drawQuoteMark(renderer, quotedetail::glyphX(area), quotedetail::glyphY(area));
    int lineTop = top;
    const int lineHeight = size18 ? area.lineHeight18 : area.lineHeight16;
    for (int line = first; line < end; ++line) {
      renderer.drawText(size18 ? NOTOSERIF_18_FONT_ID : NOTOSERIF_16_FONT_ID, quotedetail::bodyX0(area), lineTop,
                        lines[line].c_str());
      lineTop += lineHeight;
    }
    if (quotePage + 1 == bodyPages) visitX3Meta(true, 0, lineTop + 8);
  } else if (loaded) {
    visitX3Meta(true, metaOnLast + (quotePage - bodyPages) * metaPerPage, top);
  }
}

// tenor/ugly: the quote as the pen sets it. The words are laid out in the pen's own widths (the same ones the page is drawn
// with), so a line never runs past its column and a long quote turns pages without losing a word.
void QuoteDetailActivity::layoutUglyBody() {
  using namespace ugly::quotepage;
  ugly::ensureFonts(renderer);
  uglyBody = utf8ComposeNfc(endsWithCloseQuote(quote.text) ? quote.text : quote.text + CLOSE_QUOTE);
  const auto body = bodyArea();
  const int space = ugly::width(renderer, ugly::Size::S30, "a") / 2 + 4;
  uglyMap = paginate(uglyBody.data(), static_cast<int>(uglyBody.size()), renderer.getScreenWidth() - COLUMN_INSET, space,
                     capacityFor(body.y, body.bottom()), [this](const char* bytes, const int length) {
                       return ugly::width(renderer, ugly::Size::S30, std::string(bytes, length).c_str());
                     });
}

void QuoteDetailActivity::drawUglyPage() const {
  using namespace ugly::quotepage;
#ifdef UGLY_FRAME_LOG
  const unsigned long penStart = millis();
#endif
  const auto body = bodyArea();
  const int w = renderer.getScreenWidth();
  const int space = ugly::width(renderer, ugly::Size::S30, "a") / 2 + 4;
  const int rowsTop = body.y + BODY_BASE - BODY_ASCENT;
  // Every part of the page that is words, kept clear by the scribbles below.
  ugly::scrap::Rect words[24];
  int wordRects = 0;
  const auto keepClear = [&](const ugly::scrap::Rect& rect) {
    if (wordRects < 24) words[wordRects++] = rect;
  };
  // the margin of a school notebook, the line of abuse and the rule under it
  ugly::line(renderer, 26, body.y + 32, 27, body.bottom() - 20, 501, 1);
  [[maybe_unused]] const int quipRows = ugly::paragraph(renderer, ugly::Size::S22, COLUMN_X, body.y + QUIP_BASE, w - COLUMN_INSET, QUIP_PITCH,
                                       I18N.get(QUIP_LINES[quipIndex(ids[index])]));
  ugly::underline(renderer, COLUMN_X, w - 32, body.y + RULE_Y, 17, 2);
  keepClear({COLUMN_X - 6, body.y + QUIP_BASE - 26, w - 26, body.y + RULE_Y + 8});
  const bool onBody = quotePage < bodyPages;
  if (onBody) {
    if (quotePage == 0) {
      ugly::text(renderer, ugly::Size::S38, 42, body.y + MARK_BASE, OPEN_QUOTE);
      keepClear({36, body.y + MARK_BASE - 38, 84, body.y + MARK_BASE + 10});
    }
    const int rows = quotePage + 1 < bodyPages ? linesPerPage : uglyMap.lastLines;
    int pos = uglyMap.start[quotePage];
    for (int row = 0; row < rows; ++row) {
      const int baseline = body.y + BODY_BASE + row * ROW_PITCH;
      int lineWidth = 0;
      pos = nextLine(
          uglyBody.data(), static_cast<int>(uglyBody.size()), pos, w - COLUMN_INSET, space,
          [this](const char* bytes, const int length) {
            return ugly::width(renderer, ugly::Size::S30, std::string(bytes, length).c_str());
          },
          [&](const int from, const int length, const int x) {
            const std::string word = uglyBody.substr(from, length);
            ugly::text(renderer, ugly::Size::S30, COLUMN_X + x, baseline, word.c_str());
#ifdef UGLY_FRAME_LOG
            if (!renderer.isFontCacheScanning())
              LOG_INF("UGLY", "QuoteWord page=%d row=%d x=%d y=%d %s", quotePage, row, COLUMN_X + x, baseline, word.c_str());
#endif
          },
          nullptr, &lineWidth);
      keepClear({COLUMN_X - 6, baseline - BODY_ASCENT - 8, COLUMN_X + lineWidth + 8, baseline + BODY_DESCENT + 4});
    }
  }
  const int metaStart = onBody ? 0 : metaOnLast + (quotePage - bodyPages) * metaPerPage;
  const int metaTop = onBody ? rowsTop + uglyMap.lastLines * ROW_PITCH + META_GAP : rowsTop;
  if (!onBody || quotePage + 1 == bodyPages) {
    visitX3Meta(true, metaStart, metaTop);
    const int step = metaStep();
    const int shown = std::min(visitX3Meta(false) - metaStart, std::max(0, (body.bottom() - metaTop) / step));
    if (shown > 0) keepClear({24, metaTop, w - 24, metaTop + shown * step});
  }
  // The scribbles of the empty part of the page, the same ones every time this quote's page is drawn.
  ugly::scrap::Item scraps[ugly::scrap::MAX_ITEMS];
  int scrapCount = 0;
  bool scribble = wordRects < 24;
#ifdef SIMULATOR
  if (const char* env = std::getenv("CROSSPOINT_SIM_UGLY_SCRAP")) scribble = scribble && std::atoi(env) != 0;
#endif
  if (scribble) {
    scrapCount = ugly::scrap::place({44, body.y, w - 20, body.bottom() - 6}, words, wordRects,
                                    ugly::scrap::seedOf(ids[index], quotePage), scraps, ugly::scrap::MAX_ITEMS);
    ugly::scrap::draw(renderer, scraps, scrapCount);
  }
#ifdef UGLY_FRAME_LOG
  if (!renderer.isFontCacheScanning()) {
    int lowest = body.y;
    for (int i = 0; i < wordRects; ++i) lowest = std::max(lowest, words[i].y1);
    LOG_INF("UGLY", "QuotePage quote=%d page=%d/%d bodyPages=%d rows=%d lines=%d quip=%d quipRows=%d scraps=%d band=%d ms=%lu free=%u largest=%u %s",
            index, quotePage + 1, pages, bodyPages, linesPerPage, uglyMap.lines, quipIndex(ids[index]), quipRows, scrapCount,
            body.bottom() - 6 - lowest - ugly::scrap::CLEAR, millis() - penStart, static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
            I18N.get(QUIP_LINES[quipIndex(ids[index])]));
    for (int i = 0; i < scrapCount; ++i)
      LOG_INF("UGLY", "QuoteScrap page=%d kind=%d pic=%d box=%d,%d,%d,%d", quotePage, static_cast<int>(scraps[i].kind),
              scraps[i].pic, scraps[i].box.x0, scraps[i].box.y0, scraps[i].box.x1, scraps[i].box.y1);
    for (int i = 0; i < wordRects; ++i)
      LOG_INF("UGLY", "QuoteWords page=%d box=%d,%d,%d,%d", quotePage, words[i].x0, words[i].y0, words[i].x1, words[i].y1);
  }
#endif
}

bool QuoteDetailActivity::penPage() const {
  return shell::isUgly() && (tenorchrome::kTouchShell || tenorchrome::x3ScreenLayoutEnabled());
}

tenorchrome::X3ScreenRect QuoteDetailActivity::bodyArea() const {
  if (!tenorchrome::kTouchShell) return screenLayout().bodyRect;
  const int top = quotesHeaderBottom();
  return {0, top, renderer.getScreenWidth(), metrics().bandBottom - top};
}
