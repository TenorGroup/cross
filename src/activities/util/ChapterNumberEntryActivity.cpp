#include "ChapterNumberEntryActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
ChapterNumberEntryActivity::ChapterNumberEntryActivity(GfxRenderer& renderer, MappedInputManager& input, uint32_t value)
    : Activity("ChapterNumberEntry", renderer, input) {
  snprintf(digits, sizeof(digits), "%06lu", static_cast<unsigned long>(value > 999999 ? 1 : value));
}
void ChapterNumberEntryActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}
void ChapterNumberEntryActivity::loop() {
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Back)) {
    ActivityResult r;
    r.isCancelled = true;
    setResult(std::move(r));
    finish();
    return;
  }
  if (mappedInput.wasReleased(B::Confirm)) {
    const auto value = static_cast<uint32_t>(strtoul(digits, nullptr, 10));
    if (value) {
      setResult(IntervalResult{value});
      finish();
    }
    return;
  }
  int shift = mappedInput.wasReleased(B::Up) ? -1 : mappedInput.wasReleased(B::Down) ? 1 : 0;
  int delta = mappedInput.wasReleased(B::Left) ? 1 : mappedInput.wasReleased(B::Right) ? -1 : 0;
  if (shift || delta) {
    {
      RenderLock lock(*this);
      cursor = (cursor + shift + 6) % 6;
      if (delta) digits[cursor] = '0' + (digits[cursor] - '0' + delta + 10) % 10;
    }
    requestUpdate();
  }
}
void ChapterNumberEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawNavigationHeader(tr(STR_GO_TO_CHAPTER_NUMBER));
  const int w = renderer.getScreenWidth();
  const auto finishFrame = [this] {
    tenorchrome::drawTip(renderer, tr(STR_CHAPTER_NUMBER_KEYS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
  };
  if (shell::uglyParts()) {
    // tenor/ugly on the button readers: the question and the 6 digits written by hand, the one being changed
    // underlined in pen. The header, the tip and the key bar are the shared ones.
    [[maybe_unused]] const uint32_t started = millis();
    const char* label = tr(STR_CHAPTER_NUMBER_LABEL);
    const int top = tenorchrome::contentTop();
    ugly::text(renderer, ugly::Size::S30, (w - ugly::width(renderer, ugly::Size::S30, label)) / 2, top + 48, label);
    const int cell = (w - 48) / 6;
    for (int i = 0; i < 6; ++i) {
      const char ch[2] = {digits[i], 0};
      const int dw = ugly::width(renderer, ugly::Size::S52, ch), x = 24 + i * cell + (cell - dw) / 2;
      ugly::text(renderer, ugly::Size::S52, x, top + 150, ch);
      if (i == cursor) ugly::underline(renderer, x - 8, x + dw + 8, top + 166, 850, 4);
    }
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "ChapterNumber frame cursor=%d total=%lums", cursor, static_cast<unsigned long>(millis() - started));
#endif
    finishFrame();
    return;
  }
  renderer.drawCenteredText(UI_12_FONT_ID, tenorchrome::contentTop() + 24, tr(STR_CHAPTER_NUMBER_LABEL));
  const bool enlarged = normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
  const int cell = (w - 48) / 6;
  const int y = tenorchrome::contentTop() + (enlarged ? 24 + renderer.getLineHeight(UI_12_FONT_ID) + 32 : 94);
  const int cellHeight = enlarged ? renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 20 : 60;
  for (int i = 0; i < 6; ++i) {
    const int x = 24 + i * cell;
    renderer.fillRectDither(x, y, cell - 5, cellHeight, i == cursor ? Color::Black : Color::White);
    char ch[2] = {digits[i], 0};
    renderer.drawText(NOTOSANS_18_FONT_ID,
                      x + (cell - 5 - renderer.getTextWidth(NOTOSANS_18_FONT_ID, ch, EpdFontFamily::BOLD)) / 2, y + 10,
                      ch, i != cursor, EpdFontFamily::BOLD);
  }
  finishFrame();
}
