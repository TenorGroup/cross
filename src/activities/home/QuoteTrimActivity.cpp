#include "QuoteTrimActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "QuoteDetailActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

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

int QuoteTrimActivity::areaTop() const { return quotesHeaderBottom() + TOP_GAP; }

int QuoteTrimActivity::linesPerArea() const {
  const int bottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight -
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
  drawNavigationHeader(tr(STR_QUOTES_MENU_EDIT));

  const int lineHeight = renderer.getLineHeight(WORD_FONT_ID);
  const int ascender = renderer.getFontAscenderSize(WORD_FONT_ID);
  const int perArea = linesPerArea();
  const int active = movingEnd ? last : first;
  const int firstLine = firstShownLine();
  const int shownLines = std::min(perArea, lineCount - firstLine);
  const int top = areaTop();

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
        renderer.fillRect(word.x, y + ascender * 7 / 10, width, STRIKE_HEIGHT);
      }
      if (index == active)
        renderer.fillRect(word.x, y + ascender + UNDERLINE_GAP, word.width, UNDERLINE_HEIGHT);
    }
    int y = top + shownLines * lineHeight + STATUS_GAP;
    renderer.drawText(STATUS_FONT_ID, TEXT_X0, y, keep);
    y += renderer.getLineHeight(STATUS_FONT_ID) + STATUS_STEP_PAD;
    renderer.drawText(STATUS_FONT_ID, TEXT_X0, y, moving);
    y += renderer.getLineHeight(STATUS_FONT_ID) + STATUS_STEP_PAD + HELP_GAP;
    for (const char* help : {tr(STR_QUOTES_TRIM_HELP_MOVE), tr(STR_QUOTES_TRIM_HELP_SWITCH), tr(STR_QUOTES_TRIM_HELP_BACK)}) {
      renderer.drawText(HELP_FONT_ID, TEXT_X0, y, help);
      y += renderer.getLineHeight(HELP_FONT_ID);
    }
  }

  // Back discards the trim, so its hint says Cancel rather than showing the Back symbol.
  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_QUOTES_SAVE), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (failure) {
    // drawPopup lays its box over the frame and refreshes the panel itself.
    GUI.drawPopup(renderer, failure);
    return;
  }
  renderer.displayBuffer();
}
