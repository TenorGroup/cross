#include "QuoteTrimActivity.h"
#include "X3FileReaderChrome.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "QuoteDetailActivity.h"
#include "components/PageScrollbar.h"
#include "components/TouchScroll.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"

namespace {

// The words in Noto Serif 16, the two status lines in the UI subtitle tier and the help
// in the caption tier (mockup E2); all of them live in flash.
constexpr int WORD_FONT_ID = NOTOSERIF_16_FONT_ID;
constexpr int STATUS_FONT_ID = UI_10_FONT_ID;
constexpr int HELP_FONT_ID = SMALL_FONT_ID;

constexpr int TEXT_X0 = 40;
constexpr int TEXT_RIGHT_INSET = 40;
constexpr int TOP_GAP = 60;         // header bottom to the first line of words
constexpr int STATUS_GAP = 30;      // last line of words to the first status line
constexpr int STATUS_STEP_PAD = 8;  // added to the status font's line height
constexpr int HELP_GAP = 18;        // second status line to the help
constexpr int BOTTOM_MARGIN = 12;   // help to the button hints
constexpr int STRIKE_HEIGHT = 2;
constexpr int UNDERLINE_GAP = 4;    // baseline to the underline of the active word
constexpr int UNDERLINE_HEIGHT = 3;
constexpr unsigned long FAILURE_MS = 1500;

int statusBlockHeight(const GfxRenderer& renderer) {
  return STATUS_GAP + 2 * (renderer.getLineHeight(STATUS_FONT_ID) + STATUS_STEP_PAD) + HELP_GAP +
         3 * renderer.getLineHeight(HELP_FONT_ID) + BOTTOM_MARGIN;
}

}  // namespace

void QuoteTrimActivity::onEnter() {
  Activity::onEnter();
  {
    RenderLock lock(*this);
    if (quotes::load(id, quote)) layoutWords();
  }
  if (words.empty()) {
    cancel();
    return;
  }
  logArea();
  requestUpdate();
}

// Words split on single spaces, exactly as quotes::wordCount and quotes::trimWords count
// them, so the index under the cursor is the index the trim drops at.
void QuoteTrimActivity::layoutWords() {
  words.clear();
  const std::string& text = quote.text;
  const int x1 = renderer.getScreenWidth() - TEXT_RIGHT_INSET;
  const int space = renderer.getSpaceWidth(WORD_FONT_ID);
  int x = TEXT_X0;
  int line = 0;
  size_t begin = 0;
  words.reserve(quotes::wordCount(text));
  while (begin <= text.size()) {
    size_t end = text.find(' ', begin);
    if (end == std::string::npos) end = text.size();
    Word word;
    word.start = static_cast<uint16_t>(begin);
    word.length = static_cast<uint16_t>(end - begin);
    word.width = static_cast<int16_t>(renderer.getTextWidth(WORD_FONT_ID, wordText(word)));
    if (x > TEXT_X0 && x + word.width > x1) {
      ++line;
      x = TEXT_X0;
    }
    word.x = static_cast<int16_t>(x);
    word.line = static_cast<uint16_t>(line);
    x += word.width + space;
    words.push_back(word);
    begin = end + 1;
  }
  lineCount = line + 1;
  first = 0;
  last = static_cast<int>(words.size()) - 1;
  movingEnd = true;
}

// The word as a C string, in a buffer the next call reuses. Only one thread asks at a time:
// layoutWords() runs under the render lock, render() holds it.
const char* QuoteTrimActivity::wordText(const Word& word) const {
  scratch.assign(quote.text.data() + word.start, word.length);
  return scratch.c_str();
}

int QuoteTrimActivity::areaTop() const {
  return tenorchrome::x3ScreenLayoutEnabled() ? screenLayout().bodyRect.y : quotesHeaderBottom() + TOP_GAP;
}

int QuoteTrimActivity::linesPerArea() const {
  const int bottom =
      tenorchrome::x3ScreenLayoutEnabled()
          ? screenLayout().bodyRect.bottom() - tenorchrome::tipHeight(renderer, screenInfo().c_str(), 4) - 8
          : renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight -
                statusBlockHeight(renderer);
  return std::max(1, (bottom - areaTop()) / renderer.getLineHeight(WORD_FONT_ID));
}

// A quote longer than the area pages by whole areas, following the word being moved.
int QuoteTrimActivity::firstShownLine() const {
  const int active = movingEnd ? last : first;
  const int perArea = linesPerArea();
  return active < static_cast<int>(words.size()) ? words[active].line / perArea * perArea : 0;
}

// For the simulator journeys (test/reading_stats_simulator/test_quotes_v1011.py); device
// builds carry none of it.
void QuoteTrimActivity::logArea() const {
#ifdef SIMULATOR
  const int active = movingEnd ? last : first;
  const int top = firstShownLine();
  LOG_DBG("QTS", "Quote trim %s word %d of %d on line %d, lines %d-%d", movingEnd ? "last" : "first", active + 1,
          static_cast<int>(words.size()), words[active].line, top, std::min(lineCount, top + linesPerArea()) - 1);
#endif
}

// The start boundary moves within [0, last] and the end boundary within [first, last word],
// so at least one word is always kept.
void QuoteTrimActivity::move(const int direction) {
  const int lastWord = static_cast<int>(words.size()) - 1;
  if (movingEnd)
    last = std::max(first, std::min(lastWord, last + direction));
  else
    first = std::max(0, std::min(last, first + direction));
  logArea();
  requestUpdate();
}

void QuoteTrimActivity::setBoundary(const int word) {
  if (word < first || (word <= last && word - first <= last - word)) {
    movingEnd = false;
    first = std::min(word, last);
  } else {
    movingEnd = true;
    last = std::max(word, first);
  }
  logArea();
  requestUpdate();
}

int QuoteTrimActivity::wordAt(const int x, const int y) const {
  const int lineHeight = renderer.getLineHeight(WORD_FONT_ID);
  const int top = areaTop(), firstLine = firstShownLine();
  if (y < top) return -1;
  const int line = firstLine + (y - top) / lineHeight;
  if (line >= std::min(lineCount, firstLine + linesPerArea())) return -1;
  const int space = renderer.getSpaceWidth(WORD_FONT_ID);
  for (size_t i = 0; i < words.size(); ++i)
    if (words[i].line == line && x >= words[i].x - space / 2 && x < words[i].x + words[i].width + space / 2)
      return static_cast<int>(i);
  return -1;
}

void QuoteTrimActivity::save() {
  const int total = static_cast<int>(words.size());
  const size_t front = static_cast<size_t>(first);
  const size_t back = static_cast<size_t>(total - 1 - last);
  if (front == 0 && back == 0) {
    cancel();
    return;
  }
  QuoteRecord updated = quote;
  if (!quotes::trimWords(updated, front, back) || !quotes::replace(id, updated)) {
    LOG_ERR("QTS", "Quote trim failed %016llx", static_cast<unsigned long long>(id));
    failure = tr(STR_QUOTES_EDIT_FAILED);
    failureAt = millis();
    requestUpdate();
    return;
  }
  LOG_INF("QTS", "Quote trimmed %016llx: kept %d of %d words", static_cast<unsigned long long>(id), last - first + 1,
          total);
  setResult(ActivityResult());
  finish();
}

void QuoteTrimActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void QuoteTrimActivity::loop() {
  using Button = MappedInputManager::Button;
  if (failure) {
    if (millis() - failureAt >= FAILURE_MS) {
      failure = nullptr;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    cancel();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    save();
    return;
  }
  // Touch: tap a word to make it the first or the last word kept; an up or down swipe moves the end being set by a
  // screen of lines, to reach words past this screen; the bar's pill saves.
  if (tenorchrome::kTouchShell) {
    int x = 0, y = 0, dy = 0;
    unsigned long heldMs = 0;
    if (mappedInput.wasScreenTapped(x, y)) {
      if (tenorchrome::footActionAt(x, y) == 0) {
        save();
        return;
      }
      const int word = wordAt(x, y);
      if (word >= 0) setBoundary(word);
      return;
    }
    if (mappedInput.wasVerticalSwipe(dy, heldMs)) {
      const int lines = touchscroll::rows(dy, heldMs, renderer.getLineHeight(WORD_FONT_ID), linesPerArea());
      const int active = movingEnd ? last : first;
      const int line = std::max(0, std::min(lineCount - 1, static_cast<int>(words[active].line) + lines));
      int target = active;
      while (target + 1 < static_cast<int>(words.size()) && words[target].line < line) ++target;
      while (target > 0 && words[target].line > line) --target;
      move(target - active);
      return;
    }
  }
  if (mappedInput.wasReleased(Button::PageForward) || mappedInput.wasReleased(Button::PageBack)) {
    movingEnd = !movingEnd;
    logArea();
    requestUpdate();
    return;
  }
  navigator.onPressAndContinuous({Button::Right}, [this] { move(1); });
  navigator.onPressAndContinuous({Button::Left}, [this] { move(-1); });
}

void QuoteTrimActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const bool x3 = tenorchrome::x3ScreenLayoutEnabled();
  if (x3)
    x3readerchrome::drawPath(renderer, screenLayout().barRect, icon_tenor_reader_tools_40, tr(STR_QUOTES_MENU_EDIT));
  else
    drawNavigationHeader(tr(STR_QUOTES_MENU_EDIT));

  const int lineHeight = renderer.getLineHeight(WORD_FONT_ID);
  const int ascender = renderer.getFontAscenderSize(WORD_FONT_ID);
  const int perArea = linesPerArea();
  const int active = movingEnd ? last : first;
  const int firstLine = firstShownLine();
  const int shownLines = std::min(perArea, lineCount - firstLine);
  const int top = areaTop();
  const bool byHand = shell::uglyParts();

  char keep[48];
  snprintf(keep, sizeof(keep), tr(STR_QUOTES_TRIM_KEEP), last - first + 1, static_cast<int>(words.size()));
  const char* moving = movingEnd ? tr(STR_QUOTES_TRIM_END) : tr(STR_QUOTES_TRIM_START);

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  for (int pass = 0; pass < 2; ++pass) {
    if (pass == 1) scope.endScanAndPrewarm();
    for (size_t i = 0; i < words.size(); ++i) {
      const auto& word = words[i];
      if (word.line < firstLine || word.line >= firstLine + shownLines) continue;
      const int y = top + (word.line - firstLine) * lineHeight;
      renderer.drawText(WORD_FONT_ID, word.x, y, wordText(word));
      const int index = static_cast<int>(i);
      if (index < first || index > last) {
        // The strike runs on through the space when the next dropped word shares the line,
        // so a dropped stretch reads as one struck phrase.
        int width = word.width;
        const bool nextDropped = index + 1 < static_cast<int>(words.size()) && (index + 1 < first || index + 1 > last);
        if (nextDropped && words[i + 1].line == word.line) width = words[i + 1].x - word.x;
        if (byHand) ugly::line(renderer, word.x, y + ascender * 7 / 10 + 1, word.x + width, y + ascender * 7 / 10 - 1, 900 + index, 2);
        else renderer.fillRect(word.x, y + ascender * 7 / 10, width, STRIKE_HEIGHT);
      }
      if (index == active) {
        if (byHand) ugly::underline(renderer, word.x, word.x + word.width, y + ascender + UNDERLINE_GAP + 2, 905, 3);
        else renderer.fillRect(word.x, y + ascender + UNDERLINE_GAP, word.width, UNDERLINE_HEIGHT);
      }
    }
    // tenor/ugly on the button readers: the count, the end being moved and the keys written by hand; the words of
    // the quote keep their book font.
    const auto line = [&](const int fontId, const int y, const char* text) {
      if (byHand) ugly::text(renderer, ugly::Size::S22, TEXT_X0, y + renderer.getFontAscenderSize(fontId), text);
      else renderer.drawText(fontId, TEXT_X0, y, text);
    };
    if (x3) continue;
    int y = top + shownLines * lineHeight + STATUS_GAP;
    line(STATUS_FONT_ID, y, keep);
    y += renderer.getLineHeight(STATUS_FONT_ID) + STATUS_STEP_PAD;
    line(STATUS_FONT_ID, y, moving);
    y += renderer.getLineHeight(STATUS_FONT_ID) + STATUS_STEP_PAD + HELP_GAP;
    // Touch: the help names the tap, not keys the board does not have.
    const char* helps[] = {tr(STR_QUOTES_TRIM_HELP_MOVE), tr(STR_QUOTES_TRIM_HELP_SWITCH), tr(STR_QUOTES_TRIM_HELP_BACK)};
    const char* touchHelps[] = {tr(STR_QUOTES_TRIM_HELP_TAP), tr(STR_QUOTES_TRIM_HELP_BACK), nullptr};
    for (const char* help : tenorchrome::kTouchShell ? touchHelps : helps) {
      if (!help) continue;
      line(HELP_FONT_ID, y, help);
      y += byHand ? std::max(renderer.getLineHeight(HELP_FONT_ID), 30) : renderer.getLineHeight(HELP_FONT_ID);
    }
  }

  if (x3) {
    drawX3Content();
  }

  // Touch: Save is a pill in the bar; its "<" discards.
  if (tenorchrome::kTouchShell) tenorchrome::noteFootActions(tr(STR_QUOTES_SAVE));
  // Back discards the trim, so its hint says Cancel rather than showing the Back symbol.
  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_QUOTES_SAVE), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (tenorchrome::kTouchShell) {
    auto target = makeUiTarget(renderer);
    PageScrollbar::instance().draw(renderer, target, top, top + perArea * lineHeight,
                                   lineCount * lineHeight, firstLine * lineHeight, this);
  }
#ifdef UGLY_FRAME_LOG
  if (byHand) LOG_INF("UGLY", "QuoteTrim frame first=%d last=%d end=%d", first, last, movingEnd);
#endif
  if (failure) {
    // drawPopup lays its box over the frame and refreshes the panel itself.
    GUI.drawPopup(renderer, failure);
    return;
  }
  renderer.displayBuffer();
}

std::string QuoteTrimActivity::screenTip() const {
  return std::string(tr(STR_QUOTES_TRIM_HELP_MOVE)) + "\n" + tr(STR_QUOTES_TRIM_HELP_SWITCH) + "\n" +
         tr(STR_QUOTES_TRIM_HELP_BACK);
}

std::string QuoteTrimActivity::screenInfo() const {
  char keep[64];
  snprintf(keep, sizeof(keep), tr(STR_QUOTES_TRIM_KEEP), last - first + 1, static_cast<int>(words.size()));
  return std::string(keep) + "\n" + (movingEnd ? tr(STR_QUOTES_TRIM_END) : tr(STR_QUOTES_TRIM_START));
}

tenorchrome::X3ScreenLayout QuoteTrimActivity::screenLayout() const {
  const auto tip = screenTip();
  return tenorchrome::x3ScreenLayout(renderer, tenorchrome::tipHeight(renderer, tip.c_str(), 12));
}

void QuoteTrimActivity::drawX3Content() const {
  const auto layout = screenLayout();
  const auto info = screenInfo();
  const int height = tenorchrome::tipHeight(renderer, info.c_str(), 4);
  tenorchrome::drawInfoInRect(renderer, info.c_str(),
                              {layout.bodyRect.x, layout.bodyRect.bottom() - height, layout.bodyRect.width, height}, 4);
  const auto tip = screenTip();
  tenorchrome::drawTipInRect(renderer, tip.c_str(), layout.tipRect, 12);
}
