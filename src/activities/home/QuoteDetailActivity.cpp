#include "QuoteDetailActivity.h"

#include <Epub.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "QuoteTrimActivity.h"
#include "components/QuoteMarkGlyph.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "fontIds.h"

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
  std::string line = renderer.truncatedText(META_FONT_ID, chapterLine(quote, openBook.get()).c_str(), width);
  tidyEllipsis(line);
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
  // Front pair: the quote before or after, in the list's order, stopping at both ends.
  const bool next = mappedInput.wasReleased(Button::Right);
  if (next || mappedInput.wasReleased(Button::Left)) {
    const int target = index + (next ? 1 : -1);
    if (target < 0 || target >= static_cast<int>(ids.size())) return;
    {
      RenderLock lock(*this);
      show(target);
    }
    logShown();
    requestUpdate();
    return;
  }
  // Side buttons: the pages of a quote too long for one screen.
  const bool forward = mappedInput.wasReleased(Button::PageForward);
  if (forward || mappedInput.wasReleased(Button::PageBack)) {
    const int target = quotePage + (forward ? 1 : -1);
    if (target < 0 || target >= pages) return;
    quotePage = target;
    logShown();
    requestUpdate();
  }
}

void QuoteDetailActivity::drawQuote() {
  drawNavigationHeader(tr(STR_QUOTES));
  const auto m = metrics();
  char position[24];
  snprintf(position, sizeof(position), "%d/%d", index + 1, static_cast<int>(ids.size()));
  renderer.drawText(POSITION_FONT_ID,
                    quotedetail::headerPositionRight(m) - renderer.getTextWidth(POSITION_FONT_ID, position),
                    headerTextY(renderer, POSITION_FONT_ID), position);

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
    renderer.fillRect(meta.x, meta.ruleY, quotedetail::META_RULE_WIDTH, quotedetail::META_RULE_HEIGHT);
    const int titleStep = renderer.getLineHeight(TITLE_FONT_ID);
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
  // The popup draws its own button hints over the screen under it.
  if (popup.processRender(renderer, mappedInput)) return;
  // Each hint names what its button does now: at either end of the list the arrow that
  // would leave it has nothing to do, so it is not shown.
  const bool hasPrevious = index > 0, hasNext = index + 1 < static_cast<int>(ids.size());
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_QUOTES_OPTIONS), hasPrevious ? tr(STR_DIR_LEFT) : "",
                                            hasNext ? tr(STR_DIR_RIGHT) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (failure) {
    // drawPopup lays its box over the frame and refreshes the panel itself.
    GUI.drawPopup(renderer, failure);
    return;
  }
  renderer.displayBuffer();
}
