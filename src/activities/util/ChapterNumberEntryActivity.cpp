#include "ChapterNumberEntryActivity.h"

#include <I18n.h>

#include <cstdio>
#include <cstdlib>

#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "fontIds.h"
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
  renderer.drawCenteredText(UI_12_FONT_ID, tenorchrome::CONTENT_TOP + 24, tr(STR_CHAPTER_NUMBER_LABEL));
  const int cell = (w - 48) / 6, y = tenorchrome::CONTENT_TOP + 94;
  for (int i = 0; i < 6; ++i) {
    const int x = 24 + i * cell;
    renderer.fillRectDither(x, y, cell - 5, 60, i == cursor ? Color::Black : Color::White);
    char ch[2] = {digits[i], 0};
    renderer.drawText(NOTOSANS_18_FONT_ID,
                      x + (cell - 5 - renderer.getTextWidth(NOTOSANS_18_FONT_ID, ch, EpdFontFamily::BOLD)) / 2, y + 10,
                      ch, i != cursor, EpdFontFamily::BOLD);
  }
  tenorchrome::drawTip(renderer, tr(STR_CHAPTER_NUMBER_KEYS));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
