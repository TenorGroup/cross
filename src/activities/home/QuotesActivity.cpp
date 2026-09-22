#include "QuotesActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

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
  m.quoteLineHeight = static_cast<int16_t>(renderer.getLineHeight(SETTINGS.getReaderFontId()));
  m.sourceLineHeight = static_cast<int16_t>(renderer.getLineHeight(SOURCE_FONT_ID));
  return m;
}

// Reads one page of the store and wraps it once, here, so a cursor move repaints without
// measuring a single glyph again. Entries keep the wrapped lines rather than the quote
// text: the detail view reloads the whole quote from the card when it is opened.
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

  const int fontId = SETTINGS.getReaderFontId();
  const int textWidth = quoteblock::place(metrics(), 0, 1, false).textWidth;

  count = 0;
  const auto push = [&](const Entry::Kind kind, const char* label) {
    entries[count].kind = kind;
    entries[count].lines.assign(1, label);
    entries[count].source.clear();
    ++count;
  };
  if (hasPrevious) push(Entry::Kind::Previous, tr(STR_QUOTES_PREVIOUS));

  // One batch pass over every preview before anything is measured: an SD-card reading
  // font otherwise loads glyphs one overflow slot at a time, once per quote.
  std::vector<QuoteRecord> page;
  page.reserve(names.size());
  std::string charset;
  charset.reserve(PREVIEW_BYTES * 4);
  for (const auto& name : names) {
    QuoteRecord quote;
    if (!quotes::load(name, quote)) quote.text.clear();
    quote.text = clipped(quote.text, PREVIEW_BYTES);
    if (charset.size() < PREVIEW_BYTES * 4) charset += quote.text;
    page.push_back(std::move(quote));
  }
  if (!charset.empty()) renderer.ensureSdCardFontReady(fontId, charset.c_str(), 0x01 /* REGULAR */);

  for (auto& quote : page) {
    Entry& entry = entries[count];
    entry.kind = Entry::Kind::Quote;
    if (quote.text.empty()) {
      entry.lines.assign(1, tr(STR_QUOTES_UNREADABLE));
      entry.source.clear();
    } else {
      entry.lines = renderer.wrappedText(fontId, quote.text.c_str(), textWidth, quoteblock::MAX_LINES);
      if (entry.lines.empty()) entry.lines.assign(1, quote.text);
      entry.source = sourceLine(renderer, quote, textWidth);
    }
    ++count;
  }

  if (hasNext) push(Entry::Kind::Next, tr(STR_QUOTES_NEXT));
  if (!count) push(Entry::Kind::Empty, tr(STR_QUOTES_EMPTY));
  for (int i = 0; i < count; ++i) lineCounts[i] = static_cast<uint8_t>(entries[i].lines.size());
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
  // The quote first, its source under it: the detail view is the same block, given the
  // whole page to breathe in.
  std::string body = std::move(quote.text);
  body += "\n\n";
  body += sourceLine(renderer, quote, renderer.getScreenWidth() / 2);
  startActivityForResult(makeUniqueNoThrow<DictionaryDefinitionActivity>(
                             renderer, mappedInput, std::string(tr(STR_QUOTES)), std::move(body), false),
                         nullptr);
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
  const int fontId = SETTINGS.getReaderFontId();
  const int bottom = bandTop() + bandHeight();

  if (count > 0 && entries[0].kind == Entry::Kind::Empty) {
    const char* label = entries[0].lines.front().c_str();
    const int width = renderer.getTextWidth(UI_12_FONT_ID, label);
    renderer.drawText(UI_12_FONT_ID, (m.bandWidth - width) / 2, bandTop() + bandHeight() / 3, label);
  } else {
    top = quoteblock::followTop(lineCounts.data(), count, top, selected, static_cast<int16_t>(bandHeight()), m);
    // One prewarm scope over the whole page, the pattern the reader uses: the scan pass
    // records every codepoint, the second pass draws them from the warmed cache.
    auto* fcm = renderer.getFontCacheManager();
    auto scope = fcm->createPrewarmScope();
    for (int pass = 0; pass < 2; ++pass) {
      if (pass == 1) scope.endScanAndPrewarm();
      int16_t y = static_cast<int16_t>(bandTop());
      for (int i = top; i < count; ++i) {
        const Entry& entry = entries[i];
        const auto block = quoteblock::place(m, y, static_cast<int>(entry.lines.size()), i == selected);
        if (i > top && block.barY + block.barHeight > bottom) break;
        renderer.fillRect(block.barX, block.barY, block.barWidth, block.barHeight);
        const int lineFont = entry.kind == Entry::Kind::Quote ? fontId : UI_12_FONT_ID;
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
