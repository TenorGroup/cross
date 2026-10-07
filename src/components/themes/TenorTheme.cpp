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
    // Over each key the pen mark of what it does, or the word in hand when it is not a direction; then the strip.
    static constexpr ugly::Mark MARKS[6] = {ugly::Mark::Tick, ugly::Mark::Back, ugly::Mark::Up,
                                            ugly::Mark::Down, ugly::Mark::Left, ugly::Mark::Right};
    static constexpr int WIDE[4] = {105, 197, 331, 423}, NARROW[4] = {98, 186, 294, 382};
    const int* centres = renderer.getScreenWidth() >= 528 ? WIDE : NARROW;
    const int y = renderer.getScreenHeight() - 20;
    const char* words[4] = {btn1, btn2, btn3, btn4};
    renderer.fillRect(centres[0] - 46, y - 24, centres[3] - centres[0] + 92, 44, false);
    // The strip writes the battery at the left end and the clock at the right: a word over a key keeps off both.
    char clock[12] = "";
    if (!(clockstatus::hasValidTime() && halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1)))
      clock[0] = '\0';
    [[maybe_unused]] int wordsEnd = 0;
    const int leftEnd = 62, rightEnd = renderer.getScreenWidth() - 14 - (clock[0] ? ugly::width(renderer, ugly::Size::S22, clock) + 10 : 0);
    for (int i = 0; i < 4; ++i) {
      if (!words[i] || !*words[i]) continue;
      const int id = buttonSymbols::labelId(words[i]);
      if (id >= 0) {
        // A pen mark spans some 20 px (its table's 132/16 px each side and the 2 px stroke): it keeps off them too.
        constexpr int MARK_HALF = 10;
        const int cx = std::clamp(centres[i], leftEnd + MARK_HALF, rightEnd - MARK_HALF);
        ugly::mark(renderer, MARKS[id], cx, y);
        wordsEnd = std::max(wordsEnd, cx + MARK_HALF);
        continue;
      }
      const std::string word = ugly::fit(renderer, ugly::Size::S22, words[i], std::min(84, rightEnd - leftEnd));
      const int w = ugly::width(renderer, ugly::Size::S22, word.c_str());
      const int x = std::clamp(centres[i] - w / 2, leftEnd, rightEnd - w);
      ugly::text(renderer, ugly::Size::S22, x, y + 8, word.c_str());
      wordsEnd = std::max(wordsEnd, x + w);
    }
    tenorchrome::drawStatus(renderer);
    renderer.setOrientation(orig_orientation);
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "part=keys words_end=%d clock_at=%d", wordsEnd, clock[0] ? rightEnd + 10 : renderer.getScreenWidth());
#endif
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
  constexpr int cornerGap = 6;

  // Clear all cells before painting symbols that may extend beyond their original cell.
  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const auto bounds = buttonSymbols::horizontalBounds(labels[i], net);
    const bool compactSymbol = tenorchrome::compactFooterTips(bounds.left == 0 && bounds.right == 0);
    // Some screens paint their tip before the footer. Clear only the symbol
    // lane there so the lowered tip's descenders survive either paint order.
    const int clearTop = compactSymbol ? tenorchrome::smallFooterSymbolsTopY(renderer) : pageHeight - buttonY;
    renderer.fillRect(buttonPositions[i], clearTop, buttonWidth, pageHeight - clearTop, grayscale);
  }

  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    const int x = buttonPositions[i];
    const int top = pageHeight - buttonY;
    if (grayscale) continue;
    const auto bounds = buttonSymbols::horizontalBounds(labels[i], net);
    int iconX = x + buttonWidth / 2;
    if (bounds.left != 0 || bounds.right != 0) {
      if (i == 0) iconX = std::max(iconX, corners.leftEnd + cornerGap + bounds.left);
      if (i == 3) iconX = std::min(iconX, corners.rightStart - cornerGap - 1 - bounds.right);
    }
    if (!SETTINGS.tenorButtonSymbols ||
        !buttonSymbols::drawLabel(renderer, labels[i], iconX, iconAnchor, net)) {
      drawHintLabel(renderer, SMALL_FONT_ID, labels[i], x, buttonWidth, top, buttonHeight - vachCao - 2, textYOffset);
    }
    renderer.fillRectDither(x, pageHeight - vachCao - 1, buttonWidth, vachCao, Color::LightGray);
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
