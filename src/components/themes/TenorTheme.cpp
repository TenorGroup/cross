#include "TenorTheme.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "ClockStatus.h"
#include "CrossPointSettings.h"
#include "components/ButtonSymbols.h"
#include "components/TenorMenuChrome.h"
#include "fontIds.h"
#include "components/UITheme.h"
#include "activities/Activity.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"

void TenorTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                 const char* btn4) const {
  // Tat thanh trang thai ngoai trinh doc thi khong con nhan nut.
  if (SETTINGS.globalStatusBarHidden()) return;
  if (gpio.hasTouch()) {
    // No front buttons to label: the touch shell keeps only the status line of this band.
    if (tenorchrome::kTouchShell &&
        (renderer.getRenderMode() == GfxRenderer::BW || !renderer.grayPlanesAreAbsolute())) {
      tenorchrome::drawStatus(renderer);
    }
    return;
  }

  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  if (shell::uglyParts()) {
    ugly::statusBar(renderer, btn1, btn2, btn3, btn4);
    renderer.setOrientation(orig_orientation);
    return;
  }

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  const int buttonHeight = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int buttonY = UITheme::getInstance().getMetrics().buttonHintsHeight;  // cach day man
  constexpr int textYOffset = 4;                                   // chu nam cao hon Lyra mot chut, chua cho vach
  constexpr int vachCao = 3;                                       // vach xam duoi nhan
  constexpr int narrowButtonPositions[] = {58, 146, 254, 342};
  constexpr int wideButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool grayscale = renderer.getRenderMode() != GfxRenderer::BW && renderer.grayPlanesAreAbsolute();
  const bool lon = SETTINGS.globalStatusBarLarge();
  const int net = lon ? 18 : 14;
  const int iconAnchor = tenorchrome::statusIconTopY(pageHeight, lon) + net / 2;
  const auto corners = tenorchrome::statusCornerBounds(renderer, lon);
  constexpr int cornerGap = 9;
  bool singleCaptions[4] = {};
  int iconPositions[4];
  int leftEdge = corners.leftEnd + cornerGap;
  for (int index = 0; index < 4; ++index) {
    iconPositions[index] = buttonPositions[index] + buttonWidth / 2;
    if (!labels[index] || !*labels[index]) continue;
    const auto bounds = buttonSymbols::horizontalBounds(labels[index], net);
    if (bounds.left == 0 && bounds.right == 0) continue;
    iconPositions[index] = std::max(iconPositions[index], leftEdge + bounds.left);
    leftEdge = iconPositions[index] + bounds.right + cornerGap + 1;
  }
  int rightEdge = corners.rightStart - cornerGap - 1;
  for (int index = 3; index >= 0; --index) {
    if (!labels[index] || !*labels[index]) continue;
    const auto bounds = buttonSymbols::horizontalBounds(labels[index], net);
    if (bounds.left == 0 && bounds.right == 0) continue;
    iconPositions[index] = std::min(iconPositions[index], rightEdge - bounds.right);
    rightEdge = iconPositions[index] - bounds.left - cornerGap - 1;
  }

  // A text caption on the symbol footer is one row. One that fits its own cell stays centred in it; one too wide
  // for the cell (the larger text sizes) takes the empty cells beside it, and a caption that would touch the battery,
  // the clock or the next caption slides clear of them. Only a caption that cannot fit even then is cut short.
  std::string captions[4];
  int captionX[4] = {};
  const bool symbolFooter = tenorchrome::x3ScreenLayoutEnabled() && SETTINGS.tenorButtonSymbols;
  const bool largerText = normalizedUiTextSize(SETTINGS.uiTextSize) != 0;
  constexpr int captionGap = 6;  // a symbol or caption keeps 6 px from the battery and the clock
  constexpr int clockGap = captionGap + 2;  // the clock's first digit and a caption's last glyph both carry a side bearing
  for (int i = 0; i < 4 && symbolFooter; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const auto bounds = buttonSymbols::horizontalBounds(labels[i], net);
    if (bounds.left != 0 || bounds.right != 0) continue;
    const bool fits = renderer.wrappedText(SMALL_FONT_ID, labels[i], buttonWidth - 8, 2).size() == 1;
    if (!fits && !largerText) continue;
    int lo = corners.leftEnd + captionGap, hi = corners.rightStart - clockGap;
    // The neighbour's own ink bounds the room: a symbol stands narrower than its 80 px cell.
    for (int j = i - 1; j >= 0; --j) {
      if (!labels[j] || !*labels[j]) continue;
      const auto side = buttonSymbols::horizontalBounds(labels[j], net);
      const bool symbol = side.left != 0 || side.right != 0;
      lo = std::max(lo, symbol ? iconPositions[j] + side.right + captionGap : buttonPositions[j] + buttonWidth);
      break;
    }
    for (int j = i + 1; j < 4; ++j) {
      if (!labels[j] || !*labels[j]) continue;
      const auto side = buttonSymbols::horizontalBounds(labels[j], net);
      const bool symbol = side.left != 0 || side.right != 0;
      hi = std::min(hi, symbol ? iconPositions[j] - side.left - captionGap : buttonPositions[j]);
      break;
    }
    captions[i] = labels[i];
    if (renderer.getTextWidth(SMALL_FONT_ID, labels[i]) > hi - lo) {
      captions[i] = renderer.truncatedText(SMALL_FONT_ID, labels[i], hi - lo, EpdFontFamily::REGULAR);
    }
    const int width = renderer.getTextWidth(SMALL_FONT_ID, captions[i].c_str());
    captionX[i] = std::clamp(buttonPositions[i] + (buttonWidth - 1 - width) / 2, lo, std::max(lo, hi - width));
  }

  // Clear all cells before painting symbols that may extend beyond their original cell.
  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    singleCaptions[i] = !captions[i].empty() || (symbolFooter &&
        renderer.wrappedText(SMALL_FONT_ID, labels[i], buttonWidth - 8, 2).size() == 1);
    const auto bounds = buttonSymbols::horizontalBounds(labels[i], net);
    const bool compactSymbol = tenorchrome::compactFooterTips(bounds.left == 0 && bounds.right == 0);
    // Some screens paint their tip before the footer. Clear only the symbol
    // lane there so the lowered tip's descenders survive either paint order.
    const int clearTop = tenorchrome::x3ScreenLayoutEnabled() &&
                                 (!SETTINGS.tenorButtonSymbols || bounds.left != 0 || bounds.right != 0 || singleCaptions[i])
                             ? tenorchrome::x3ScreenLayout(renderer).buttonBandRect.y
                             : compactSymbol ? tenorchrome::smallFooterSymbolsTopY(renderer) : pageHeight - buttonY;
    renderer.fillRect(buttonPositions[i], clearTop, buttonWidth, pageHeight - clearTop, grayscale);
    if (!captions[i].empty()) {
      const int left = std::min(buttonPositions[i], captionX[i]);
      const int right = std::max(buttonPositions[i] + buttonWidth, captionX[i] + renderer.getTextWidth(SMALL_FONT_ID, captions[i].c_str()));
      renderer.fillRect(left, clearTop, right - left, pageHeight - clearTop, grayscale);
    }
  }

  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const int x = buttonPositions[i];
    const int top = pageHeight - buttonY;
    if (grayscale) continue;
    const int iconX = iconPositions[i];
    if (!SETTINGS.tenorButtonSymbols ||
        !buttonSymbols::drawLabel(renderer, labels[i], iconX, iconAnchor, net)) {
      if (singleCaptions[i]) {
        const int inkTop = renderer.getTextInkTop(SMALL_FONT_ID, labels[i], EpdFontFamily::REGULAR);
        const int inkBottom = renderer.getTextInkBottom(SMALL_FONT_ID, labels[i], EpdFontFamily::REGULAR);
        const int inkHeight = inkBottom - inkTop + 1;
        const int bandTop = tenorchrome::x3ScreenLayout(renderer).barRect.bottom() + 3;
        const int lastInkTop = pageHeight - vachCao - 2 - inkHeight;
        const int inkY = std::clamp(iconAnchor - (inkHeight - 1) / 2, std::min(bandTop, lastInkTop), lastInkTop);
        if (captions[i].empty()) {
          renderer.drawText(SMALL_FONT_ID, x + (buttonWidth - 1 - renderer.getTextWidth(SMALL_FONT_ID, labels[i])) / 2,
                            inkY - inkTop, labels[i]);
        } else {
          renderer.drawText(SMALL_FONT_ID, captionX[i], inkY - inkTop, captions[i].c_str());
        }
      } else {
        drawHintLabel(renderer, SMALL_FONT_ID, labels[i], x, buttonWidth, top, buttonHeight - vachCao - 2, textYOffset);
      }
    }
    renderer.fillRectDither(x, pageHeight - vachCao, buttonWidth, vachCao, Color::LightGray);
  }

  if (!grayscale) tenorchrome::drawStatus(renderer);
  renderer.setOrientation(orig_orientation);
}

void TenorTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                            bool backButton) const {
  if (tenorchrome::enabled()) {
    tenorchrome::drawHeader(renderer, title, "", subtitle);
    return;
  }
  LyraTheme::drawHeader(renderer, rect, title, subtitle, backButton);
}

void TenorTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  if (!SETTINGS.tenorSideArrows || gpio.hasTouch()) return;
  const char* labels[] = {topBtn, bottomBtn};
  for (int i = 0; i < 2; ++i) {
    if (!labels[i] || !*labels[i]) continue;
    // Callers supply the text that the legacy painter rotated clockwise.
    const auto shape = *labels[i] == '^'   ? inlineSymbols::Shape::Left
                       : *labels[i] == 'v' ? inlineSymbols::Shape::Right
                       : *labels[i] == '>' ? inlineSymbols::Shape::Up
                                           : inlineSymbols::Shape::Down;
    const int x =
        gpio.hasEdgeSideButtons() ? (i ? renderer.getScreenWidth() - 1 - 7 : 7) : renderer.getScreenWidth() - 7;
    const int y = gpio.hasEdgeSideButtons() ? 195 : 195 + i * 83;
    if (shell::uglyParts()) {  // the pen mark, kept whole inside the edge
      const auto m = shape == inlineSymbols::Shape::Left    ? ugly::Mark::Left
                     : shape == inlineSymbols::Shape::Right ? ugly::Mark::Right
                     : shape == inlineSymbols::Shape::Up    ? ugly::Mark::Up
                                                            : ugly::Mark::Down;
      ugly::mark(renderer, m, x < renderer.getScreenWidth() / 2 ? 12 : renderer.getScreenWidth() - 12, y);
      continue;
    }
    inlineSymbols::drawShape(renderer, shape, x, y, 8, true);
  }
}
