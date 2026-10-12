#include "ConfirmationActivity.h"

#include <algorithm>

#include <I18n.h>
#include <Logging.h>

#include "HalDisplay.h"
#include "components/ConfirmationText.h"
#include "components/DestructiveActionIcon.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "shells/Shell.h"
#include "shells/ugly/UglySwitch.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body,
                                           ConfirmationContent content)
    : Activity("Confirmation", renderer, mappedInput), heading(heading), body(body), content(std::move(content)) {}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();
  if (shell::isUgly()) ugly::ensureFonts(renderer);
  shownAt = millis();

  requestUpdate(true);
}

namespace {
void centered(const GfxRenderer& r, int font, const char* text, const ConfirmationBox& box,
              EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const auto ink = r.getTextInkBounds(font, text, style);
  r.drawText(font, box.x + (box.width - ink[2] + ink[0]) / 2 - ink[0],
             box.y + (box.height - ink[3] + ink[1]) / 2 - ink[1], text, true, style);
}

void centeredLines(const GfxRenderer& r, int font, const std::vector<std::string>& lines,
                   const ConfirmationBox& box) {
  if (lines.empty()) return;
  const int lineHeight = r.getLineHeight(font);
  int inkTop = lineHeight, inkBottom = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto ink = r.getTextInkBounds(font, lines[i].c_str(), EpdFontFamily::REGULAR);
    const int top = static_cast<int>(i) * lineHeight + (lineHeight - ink[3] + ink[1]) / 2;
    inkTop = std::min(inkTop, top);
    inkBottom = std::max(inkBottom, top + ink[3] - ink[1]);
  }
  const int y = box.y + (box.height - inkBottom + inkTop) / 2 - inkTop;
  for (size_t i = 0; i < lines.size(); ++i)
    centered(r, font, lines[i].c_str(), {box.x, y + static_cast<int>(i) * lineHeight, box.width, lineHeight});
}

void whiteHalo(const GfxRenderer& r, const ConfirmationBox& box) {
  r.fillRect(box.x - 8, box.y - 8, box.width + 16, box.height + 16, false);
}

// Find the top of the cut line in the existing framebuffer, without a second screen-sized buffer.
int wholeLineTop(const GfxRenderer& r, int top) {
  uint8_t row[800];
  for (; top > tenorchrome::contentTop(); --top) {
    const size_t bytes = r.getRegionByteSize(24, top, r.getScreenWidth() - 48, 1);
    if (bytes > sizeof(row) || !r.copyRegionToBuffer(24, top, r.getScreenWidth() - 48, 1, row, sizeof(row))) break;
    bool white = true;
    for (size_t i = 0; i < bytes; ++i) white = white && row[i] == 0xff;
    if (white) break;
  }
  return top;
}
}  // namespace

void ConfirmationActivity::layoutCross() {
  if (laidOut) return;
  const bool touch = tenorchrome::kTouchShell;
  int top = tenorchrome::contentTop();
  int bottom = tenorchrome::footBackTop(renderer.getScreenHeight()) - 8;
  int headerHeight = 0;
  const auto& message = touch && !content.touchMessage.empty() ? content.touchMessage : content.message;
  const int font = touch ? UI_10_FONT_ID : SMALL_FONT_ID;
  const int width = touch ? renderer.getScreenWidth() - 64 : 326;
  const auto& subject = message != body || !content.destructive ? body : std::string{};
  messageLines = confirmationMessageLines(
      message, subject, 3,
      [&](const char* text, int maxLines) { return renderer.wrappedText(font, text, width, maxLines); },
      [&](const char* text) { return renderer.getTextWidth(font, text); });
  if (touch) headerHeight = 52 + (static_cast<int>(messageLines.size()) - 1) * renderer.getLineHeight(font);
  if (!touch) {
    const auto bodyRect = tenorchrome::x3ScreenLayout(renderer).bodyRect;
    top = bodyRect.y;
    bottom = bodyRect.y + bodyRect.height;
    questionLines = renderer.wrappedText(UI_12_FONT_ID, content.question.c_str(), 326, 4, EpdFontFamily::BOLD);
    headerHeight = 40 + questionLines.size() * renderer.getLineHeight(UI_12_FONT_ID) + 4 +
                   messageLines.size() * renderer.getLineHeight(SMALL_FONT_ID);
  }
  layout = confirmationLayout(touch, renderer.getScreenWidth(), top, bottom, headerHeight,
                              UITheme::getInstance().getMetrics().popupCornerRadius);
  if (touch) {
    const int clearTop = wholeLineTop(renderer, layout.action.y - 8);
    renderer.fillRect(0, clearTop, renderer.getScreenWidth(), bottom + 8 - clearTop, false);
    tenorchrome::clearFootBand(renderer);
  } else {
    whiteHalo(renderer, layout.action);
  }
  laidOut = true;
}

void ConfirmationActivity::complete(bool cancelled) {
  ActivityResult res;
  res.isCancelled = cancelled;
  setResult(std::move(res));
  finish();
}

void ConfirmationActivity::render(RenderLock&& lock) {
  if (shell::isUgly()) {
    // The question box of the shell: the heading asked in a large hand, the subject under it, No first in the circle.
    const char* answers[2] = {I18N.get(StrId::STR_CONFIRM), I18N.get(StrId::STR_CANCEL)};
    ugly::askBox(renderer, mappedInput, heading.c_str(), body.c_str(), answers, uglyChoice, uglyAnswers, mark_.get(uglyChoice));
    renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "part=ask sel=%d", uglyChoice);
#endif
    return;
  }
  renderer.setRenderMode(GfxRenderer::BW);
  layoutCross();
  const auto& box = layout.action;
#ifdef SIMULATOR
  LOG_INF("DIALOG", "confirm box=%d,%d,%d,%d", box.x, box.y, box.width, box.height);
#endif
  renderer.fillRoundedRect(box.x, box.y, box.width, box.height, layout.radius, Color::White);
  tenorchrome::drawRoundRing(renderer, box.x, box.y, box.width, box.height, layout.radius, 2, tenorchrome::kTouchShell);
  if (tenorchrome::kTouchShell) {
    const auto& cancel = layout.cancel;
    renderer.fillRoundedRect(cancel.x, cancel.y, cancel.width, cancel.height, layout.radius, Color::White);
    tenorchrome::drawRoundRing(renderer, cancel.x, cancel.y, cancel.width, cancel.height, layout.radius, 2, true);
    centeredLines(renderer, UI_10_FONT_ID, messageLines, {box.x, box.y, box.width, layout.messageHeight});
    tenorchrome::drawRowRule(renderer, box.y + layout.messageHeight, box.x + 16, box.x + box.width - 16);
    const auto label = renderer.truncatedText(UI_12_FONT_ID, content.longAction.c_str(),
                                              box.width - 32 - (content.destructive ? 34 : 0));
    const auto ink = renderer.getTextInkBounds(UI_12_FONT_ID, label.c_str(), EpdFontFamily::REGULAR);
    const int width = ink[2] - ink[0];
    const int left = box.x + (box.width - width - (content.destructive ? 34 : 0)) / 2;
    const int textTop = box.y + layout.messageHeight + (60 - ink[3] + ink[1]) / 2;
    renderer.drawText(UI_12_FONT_ID, left + (content.destructive ? 34 : 0) - ink[0], textTop - ink[1], label.c_str());
    if (content.destructive) {
      const int iconTop = textTop + (ink[3] - ink[1] - 24) / 2;
      tenorchrome::drawDestructiveActionIcon(renderer, left, iconTop);
    }
    centered(renderer, UI_12_FONT_ID, tr(STR_CANCEL), cancel, EpdFontFamily::BOLD);
  } else {
    int y = box.y + 20;
    for (const auto& line : questionLines) {
      const int height = renderer.getLineHeight(UI_12_FONT_ID);
      centered(renderer, UI_12_FONT_ID, line.c_str(), {box.x + 20, y, box.width - 40, height}, EpdFontFamily::BOLD);
      y += height;
    }
    y += 4;
    for (const auto& line : messageLines) {
      const int height = renderer.getLineHeight(SMALL_FONT_ID);
      centered(renderer, SMALL_FONT_ID, line.c_str(), {box.x + 20, y, box.width - 40, height});
      y += height;
    }
    const int rowsTop = box.y + layout.messageHeight;
    tenorchrome::drawRowRule(renderer, rowsTop, box.x + 2, box.x + box.width - 2);
    tenorchrome::drawRowRule(renderer, rowsTop + 60, box.x + 2, box.x + box.width - 2);
    const char* actionLabels[] = {content.shortAction.c_str(), tr(STR_CANCEL)};
    for (int i = 0; i < 2; ++i) {
      const int rowY = rowsTop + i * 60;
      if (selectedIndex == i) tenorchrome::drawPillRing(renderer, box.x + 12, rowY + 6, box.width - 24, 48, 3, false);
      centered(renderer, UI_12_FONT_ID, actionLabels[i], {box.x, rowY, box.width, 60},
               i == 1 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void ConfirmationActivity::loop() {
  if (confirmationExpired(content.timeoutMs, shownAt, millis())) {
    complete(true);
    return;
  }
  if (shell::isUgly()) {
    using B = MappedInputManager::Button;
    if (tenorchrome::kTouchShell) {
      const auto input = touchSnapshotFrom(mappedInput);
      int answer = -1;
      if (input.touchReleased && input.touchX >= 0) {
        RenderLock lock(RenderLock::TryTake{});
        if (!lock.acquired()) return;
        for (int index = 0; index < 2; ++index) {
          const auto& box = uglyAnswers[index];
          const int top = (box.y0 + box.y1 - 66) / 2;
          if (box.x1 > box.x0 && input.touchY >= top && input.touchY < top + 66)
            answer = index;
        }
      }
      if (answer >= 0) {
        ActivityResult result;
        result.isCancelled = answer != 0;
        setResult(std::move(result));
        finish();
        return;
      }
    }
    if (mappedInput.wasReleased(B::Up) || mappedInput.wasReleased(B::Down) || mappedInput.wasReleased(B::Left) ||
        mappedInput.wasReleased(B::Right)) {
      uglyChoice = 1 - uglyChoice;
      requestUpdate();
      return;
    }
    const bool yes = mappedInput.wasReleased(B::Confirm) && uglyChoice == 0;
    if (!yes && !mappedInput.wasReleased(B::Confirm) && !mappedInput.wasReleased(B::Back)) return;
    ActivityResult res;
    res.isCancelled = !yes;
    setResult(std::move(res));
    finish();
    return;
  }
  using B = MappedInputManager::Button;
  const auto input = touchSnapshotFrom(mappedInput);
  const auto decision = confirmationDecision(
      layout, tenorchrome::kTouchShell, selectedIndex, mappedInput.wasReleased(B::Back),
      mappedInput.wasReleased(B::Confirm), mappedInput.wasPressed(B::NavPrevious) || mappedInput.wasPressed(B::NavNext),
      laidOut && input.touchReleased, input.touchX, input.touchY);
  switch (decision) {
    case ConfirmationDecision::Cancel:
      complete(true);
      break;
    case ConfirmationDecision::Accept:
      complete(false);
      break;
    case ConfirmationDecision::Move: {
      RenderLock lock(*this);
      selectedIndex = 1 - selectedIndex;
      requestUpdate();
      break;
    }
    case ConfirmationDecision::Wait:
      break;
  }
}
