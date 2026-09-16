#include "TenorTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "components/ButtonSymbols.h"
#include "components/TenorMenuChrome.h"
#include "fontIds.h"

void TenorTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                 const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }

  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int buttonHeight = TenorMetrics::values.buttonHintsHeight;
  constexpr int buttonY = TenorMetrics::values.buttonHintsHeight;  // cach day man
  constexpr int textYOffset = 4;                                   // chu nam cao hon Lyra mot chut, chua cho vach
  constexpr int vachCao = 3;                                       // vach xam duoi nhan
  constexpr int narrowButtonPositions[] = {58, 146, 254, 342};
  constexpr int wideButtonPositions[] = {65, 157, 291, 383};
  const int* buttonPositions = renderer.getScreenWidth() >= 528 ? wideButtonPositions : narrowButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const bool grayscale = renderer.getRenderMode() != GfxRenderer::BW && !renderer.grayPlanesAreAbsolute();

  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const int x = buttonPositions[i];
    const int top = pageHeight - buttonY;
    // Pha xam: to den de bit xam bang 0, giu nguyen nhan den trang cua pha don sac.
    renderer.fillRect(x, top, buttonWidth, buttonHeight, grayscale);
    if (grayscale) continue;
    if (!SETTINGS.tenorButtonSymbols || !buttonSymbols::drawLabel(renderer, labels[i], x + buttonWidth / 2, top + 17)) {
      drawHintLabel(renderer, SMALL_FONT_ID, labels[i], x, buttonWidth, top, buttonHeight - vachCao - 2, textYOffset);
    }
    renderer.fillRectDither(x, pageHeight - vachCao - 1, buttonWidth, vachCao, Color::LightGray);
  }

  if (!grayscale) tenorchrome::drawStatus(renderer);
  renderer.setOrientation(orig_orientation);
}

void TenorTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  if (tenorchrome::enabled()) {
    tenorchrome::drawHeader(renderer, title);
    return;
  }
  LyraTheme::drawHeader(renderer, rect, title, subtitle);
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
    const int x = gpio.hasEdgeSideButtons() ? (i ? renderer.getScreenWidth() - 7 : 7) : renderer.getScreenWidth() - 7;
    const int y = gpio.hasEdgeSideButtons() ? 195 : 195 + i * 83;
    inlineSymbols::drawShape(renderer, shape, x, y, 8, true);
  }
}
