#include "QuotesActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include <Epub.h>

#include "CrossPointSettings.h"
#include "activities/reader/DictionaryDefinitionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// The line under a quote names its source. It is the caption tier of the UI faces, the
// smallest one the firmware ships, so it stays clearly under the reading font whatever
// size the reader is set to.
constexpr int SOURCE_FONT_ID = SMALL_FONT_ID;

// Cut a UTF-8 string at `limit` bytes without splitting a codepoint.
std::string clipped(const std::string& text, const size_t limit) {
  if (text.size() <= limit) return text;
  size_t length = limit;
  while (length && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) --length;
  return text.substr(0, length);
}

// Book, place in it, and the day it was kept, on one line no wider than `width`. The
// book title is the part that gets shortened: a long title must not push the chapter,
// the page and the date off the end of the line.
std::string sourceLine(const GfxRenderer& renderer, const QuoteRecord& quote, const int width) {
  char place[96];
  snprintf(place, sizeof(place), tr(STR_QUOTES_SOURCE_FORMAT), quote.spine + 1, quote.page + 1,
           static_cast<unsigned>(quote.day % 100), static_cast<unsigned>(quote.day / 100 % 100),
           static_cast<unsigned>(quote.day / 10000));
  if (quote.title.empty()) return place;
  static constexpr char SEPARATOR[] = " - ";
  const int room = width - renderer.getTextWidth(SOURCE_FONT_ID, place) -
                   renderer.getTextWidth(SOURCE_FONT_ID, SEPARATOR);
  if (room <= 0) return place;
  const std::string title = renderer.truncatedText(SOURCE_FONT_ID, clipped(quote.title, 256).c_str(), room);
  return title + SEPARATOR + place;
}

// The chapter by number and, when the book's own table of contents names it, by name. The
// lookup is the one the reader's status bar makes: spine item -> table of contents entry.
// A book with no cached table of contents leaves the number standing alone.
std::string chapterLine(const QuoteRecord& quote) {
  char line[192];
  Epub epub(quote.path, "/.crosspoint");
  if (epub.load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true)) {
    const int tocIndex = epub.getTocIndexForSpineIndex(quote.spine);
    if (tocIndex >= 0) {
      const std::string title = epub.getTocItem(tocIndex).title;
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

}  // namespace

void QuotesActivity::onEnter() {
  Activity::onEnter();
  {
    RenderLock lock(*this);
    loadPage("", false);
  }
  requestUpdate();
}

int QuotesActivity::bandTop() const {
  const auto& m = UITheme::getInstance().getMetrics();
  return m.topPadding + m.headerHeight + m.verticalSpacing;
}

int QuotesActivity::bandHeight() const {
  const auto& m = UITheme::getInstance().getMetrics();
  return renderer.getScreenHeight() - m.buttonHintsHeight - bandTop();
}

quoteblock::Metrics QuotesActivity::metrics() const {
  quoteblock::Metrics m;
  m.bandX = 0;
  m.bandWidth = static_cast<int16_t>(renderer.getScreenWidth());
  m.quoteLineHeight = static_cast<int16_t>(renderer.getLineHeight(bodyFont));
  m.sourceLineHeight = static_cast<int16_t>(renderer.getLineHeight(SOURCE_FONT_ID));
  return m;
}

// The quote body is measured and drawn with a face the firmware carries in flash. Nothing
// behind getTextWidth() consults the advance table, so a reading font kept on the card
// loads every glyph it measures through an eight-slot cache: three quotes cost 6452 card
// reads and a full minute before this screen first painted.
int QuotesActivity::builtInBodyFont() const {
  const int readerFontId = SETTINGS.getReaderFontId();
  if (!renderer.isSdCardFont(readerFontId)) return readerFontId;
  return renderer.getLineHeight(readerFontId) >= renderer.getLineHeight(NOTOSERIF_18_FONT_ID)
             ? NOTOSERIF_18_FONT_ID
             : NOTOSERIF_16_FONT_ID;
}

// Wrap one block, once. A cursor move then repaints from the stored lines, and a block the
// band never reaches is never measured at all.
void QuotesActivity::ensureWrapped(const int index) {
  if (index < 0 || index >= count) return;
  Entry& entry = entries[index];
  if (entry.wrapped) return;
  entry.wrapped = true;
  const int textWidth = quoteblock::place(metrics(), 0, 1, false).textWidth;
  entry.lines = renderer.wrappedText(bodyFont, entry.preview.c_str(), textWidth, quoteblock::MAX_LINES);
  if (entry.lines.empty()) entry.lines.assign(1, entry.preview);
  entry.preview.clear();
  entry.preview.shrink_to_fit();
  lineCounts[index] = static_cast<uint8_t>(entry.lines.size());
}

// Reads one page of the store and keeps each quote's preview text. Wrapping waits until a
// block is wanted on screen: the detail view reloads the whole quote from the card anyway.
void QuotesActivity::loadPage(const std::string& boundary, const bool previous) {
  quotes::list(boundary, previous, names);
  if (previous) {
    hasPrevious = names.size() > quotes::PAGE_SIZE;
    hasNext = true;
    if (hasPrevious) names.erase(names.begin());
  } else {
    hasNext = names.size() > quotes::PAGE_SIZE;
    hasPrevious = !boundary.empty();
    if (hasNext) names.pop_back();
  }
  if (names.empty()) hasPrevious = hasNext = false;

  bodyFont = builtInBodyFont();
  const int textWidth = quoteblock::place(metrics(), 0, 1, false).textWidth;

  count = 0;
  const auto push = [&](const Entry::Kind kind, const char* label) {
    Entry& entry = entries[count];
    entry.kind = kind;
    entry.lines.assign(1, label);
    entry.preview.clear();
    entry.source.clear();
    entry.wrapped = true;
    ++count;
  };
  if (hasPrevious) push(Entry::Kind::Previous, tr(STR_QUOTES_PREVIOUS));

  for (const auto& name : names) {
    QuoteRecord quote;
    if (!quotes::load(name, quote)) quote.text.clear();
    Entry& entry = entries[count];
    entry.kind = Entry::Kind::Quote;
    entry.lines.clear();
    entry.preview.clear();
    if (quote.text.empty()) {
      entry.lines.assign(1, tr(STR_QUOTES_UNREADABLE));
      entry.source.clear();
      entry.wrapped = true;
    } else {
      entry.preview = clipped(quote.text, PREVIEW_BYTES);
      entry.source = sourceLine(renderer, quote, textWidth);
      entry.wrapped = false;
    }
    ++count;
  }

  if (hasNext) push(Entry::Kind::Next, tr(STR_QUOTES_NEXT));
  if (!count) push(Entry::Kind::Empty, tr(STR_QUOTES_EMPTY));
  // A block nobody has wrapped yet stands in as one line: enough for the band arithmetic
  // to reach it, and replaced by the real count the moment it is wrapped.
  for (int i = 0; i < count; ++i)
    lineCounts[i] = entries[i].wrapped ? static_cast<uint8_t>(entries[i].lines.size()) : 1;
  selected = 0;
  top = 0;
}

void QuotesActivity::moveSelection(const int direction) {
  if (count <= 0 || entries[0].kind == Entry::Kind::Empty) return;
  selected = direction > 0 ? ButtonNavigator::nextIndex(selected, count)
                           : ButtonNavigator::previousIndex(selected, count);
  requestUpdate();
}

int QuotesActivity::blockAt(const int y) const {
  const auto m = metrics();
  int blockY = bandTop();
  const int bottom = bandTop() + bandHeight();
  for (int i = top; i < count; ++i) {
    const int16_t height = quoteblock::blockHeight(lineCounts[i], m);
    if (blockY >= bottom) break;
    if (y >= blockY && y < blockY + height) return i;
    blockY += height;
  }
  return -1;
}

void QuotesActivity::activateIndex(const int index) {
  if (index < 0 || index >= count) return;
  const Entry::Kind kind = entries[index].kind;
  if (kind == Entry::Kind::Empty) return;
  if (kind == Entry::Kind::Previous || kind == Entry::Kind::Next) {
    const auto key = kind == Entry::Kind::Previous ? names.front() : names.back();
    {
      RenderLock lock(*this);
      loadPage(key, kind == Entry::Kind::Previous);
    }
    requestUpdate();
    return;
  }
  const int item = index - (hasPrevious ? 1 : 0);
  if (item < 0 || item >= static_cast<int>(names.size())) return;
  QuoteRecord quote;
  if (!quotes::load(names[item], quote)) return;
  startActivityForResult(makeUniqueNoThrow<DictionaryDefinitionActivity>(
                             renderer, mappedInput, std::string(tr(STR_QUOTES)), detailBody(quote), false),
                         nullptr);
}

// Where the quote came from, one line each: the book, the chapter by number and name, the
// moment it was kept, the page. The words themselves follow after a blank line, so the
// source is read before them.
std::string QuotesActivity::detailBody(const QuoteRecord& quote) const {
  std::string body;
  if (!quote.title.empty()) {
    body += clipped(quote.title, 256);
    body += "\n";
  }
  body += chapterLine(quote);
  body += "\n";
  body += whenLine(quote);
  body += "\n";
  char page[48];
  snprintf(page, sizeof(page), tr(STR_QUOTES_DETAIL_PAGE), quote.page + 1);
  body += page;
  body += "\n\n";
  body += quote.text;
  return body;
}

void QuotesActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (count <= 0 || entries[0].kind == Entry::Kind::Empty) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(selected);
    return;
  }
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int hit = blockAt(ty);
    if (hit >= 0) {
      selected = hit;
      activateIndex(hit);
    }
    return;
  }
  buttonNavigator.onNext([this] { moveSelection(1); });
  buttonNavigator.onPrevious([this] { moveSelection(-1); });
}

void QuotesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawNavigationHeader(tr(STR_QUOTES));

  const auto m = metrics();
  const int bottom = bandTop() + bandHeight();

  if (count > 0 && entries[0].kind == Entry::Kind::Empty) {
    const char* label = entries[0].lines.front().c_str();
    const int width = renderer.getTextWidth(UI_12_FONT_ID, label);
    renderer.drawText(UI_12_FONT_ID, (m.bandWidth - width) / 2, bandTop() + bandHeight() / 3, label);
  } else {
    // followTop adds up every block from the band's top down to the cursor, so those have
    // to carry their real line count before it can decide which one starts the band.
    for (int i = 0; i <= selected; ++i) ensureWrapped(i);
    top = quoteblock::followTop(lineCounts.data(), count, top, selected, static_cast<int16_t>(bandHeight()), m);
    // One prewarm scope over the whole page, the pattern the reader uses: the scan pass
    // records every codepoint, the second pass draws them from the warmed cache.
    auto* fcm = renderer.getFontCacheManager();
    auto scope = fcm->createPrewarmScope();
    for (int pass = 0; pass < 2; ++pass) {
      if (pass == 1) scope.endScanAndPrewarm();
      int16_t y = static_cast<int16_t>(bandTop());
      for (int i = top; i < count; ++i) {
        ensureWrapped(i);
        const Entry& entry = entries[i];
        const auto block = quoteblock::place(m, y, static_cast<int>(entry.lines.size()), i == selected);
        if (i > top && block.barY + block.barHeight > bottom) break;
        renderer.fillRect(block.barX, block.barY, block.barWidth, block.barHeight);
        const int lineFont = entry.kind == Entry::Kind::Quote ? bodyFont : UI_12_FONT_ID;
        for (size_t line = 0; line < entry.lines.size(); ++line) {
          renderer.drawText(lineFont, block.textX, block.textY + static_cast<int>(line) * block.lineStep,
                            entry.lines[line].c_str());
        }
        if (!entry.source.empty()) {
          renderer.drawText(SOURCE_FONT_ID, block.textX, block.sourceY, entry.source.c_str());
        }
        y = static_cast<int16_t>(y + block.height);
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
