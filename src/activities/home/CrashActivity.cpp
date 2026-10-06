#include "CrashActivity.h"

#include <GfxRenderer.h>
#include <HalSystem.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglyWords.h"

void CrashActivity::onEnter() {
  Activity::onEnter();

  panicMessage = HalSystem::getPanicInfo(false);
  if (panicMessage.empty()) {
    panicMessage = tr(STR_CRASH_NO_REASON);
  }

  requestUpdateAndWait();
}

void CrashActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void CrashActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto contentWidth = pageWidth - 2 * metrics.contentSidePadding;
  const auto x = metrics.contentSidePadding;
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  // tenor/ugly on the button readers: the words in hand, the reason as the device wrote it.
  const bool handwritten = shell::uglyParts();
  // Crash report is a dead end, not a pushed screen: no back button.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 handwritten ? ugly::words::crashTitle() : tr(STR_CRASH_TITLE), nullptr, false);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  if (handwritten) {
    static constexpr int STEP = 40;
    y += ugly::ascent(ugly::Size::S30);
    y += STEP * ugly::paragraph(renderer, ugly::Size::S30, x, y, contentWidth, STEP, ugly::words::crashBody());
    y += metrics.verticalSpacing * 2;
    ugly::text(renderer, ugly::Size::S30, x, y, ugly::words::crashReason());
    y += metrics.verticalSpacing * 2;
  } else {
    auto descLines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_CRASH_DESCRIPTION), contentWidth, 10);
    for (const auto& line : descLines) {
      renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
      y += lineHeight;
    }

    y += metrics.verticalSpacing * 2;
    renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_CRASH_REASON));
    y += lineHeight + metrics.verticalSpacing;
  }

  auto panicLines = renderer.wrappedText(UI_10_FONT_ID, panicMessage.c_str(), contentWidth, 5);
  for (const auto& line : panicLines) {
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
    y += lineHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
