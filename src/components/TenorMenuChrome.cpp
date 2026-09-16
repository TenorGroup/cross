#include "TenorMenuChrome.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "ButtonSymbols.h"
#include "UITheme.h"
#include "fontIds.h"

void tenorchrome::drawHeader(const GfxRenderer& r, const char* title, const char* prefix) {
  constexpr int x = 18, rightReserve = 18, tracking = 1;
  constexpr int font = UI_12_FONT_ID;
  constexpr auto dir = BidiUtils::BidiBaseDir::AUTO;
  const int width = std::max(1, r.getScreenWidth() - x - rightReserve);
  const int y = HEADER_TOP + (HEADER_HEIGHT - r.getLineHeight(font)) / 2;
  const auto measure = [&](const std::string& s, EpdFontFamily::Style style) {
    return r.getTextWidth(font, s.c_str(), style, dir, tracking);
  };
  const std::string leaf = r.truncatedText(font, title ? title : "", width, EpdFontFamily::BOLD, tracking);
  std::string ancestors = prefix ? prefix : "";
  if (!ancestors.empty()) ancestors += "/";
  const int room = std::max(0, width - measure(leaf, EpdFontFamily::BOLD) - tracking);
  if (measure(ancestors, EpdFontFamily::REGULAR) > room) {
    // Drop complete ancestors first, preserving readable directory names.
    std::string suffix = ancestors;
    while (!suffix.empty() && measure("…/" + suffix, EpdFontFamily::REGULAR) > room) {
      const auto slash = suffix.find('/');
      if (slash == std::string::npos)
        suffix.clear();
      else
        suffix.erase(0, slash + 1);
    }
    ancestors = "…/" + suffix;
    if (measure(ancestors, EpdFontFamily::REGULAR) > room) ancestors.clear();
  }
  r.drawText(font, x, y, ancestors.c_str(), true, EpdFontFamily::REGULAR, dir, tracking);
  const int ancestorWidth = ancestors.empty() ? 0 : measure(ancestors, EpdFontFamily::REGULAR) + tracking;
  for (int dy = 0; dy < r.getLineHeight(font); ++dy)
    for (int dx = (16 - (dy * 3 % 16)) % 16; dx < ancestorWidth; dx += 16) r.drawPixel(x + dx, y + dy, false);
  r.drawText(font, x + ancestorWidth, y, leaf.c_str(), true, EpdFontFamily::BOLD, dir, tracking);
}

// One text lane shared by every menu and reader. Only an existing render reads the clock.
void tenorchrome::drawStatus(const GfxRenderer& r, const char* title, int currentPage, int pageCount,
                             float bookProgress, int paddingBottom, bool estimated, bool bookmarked) {
  const int width = r.getScreenWidth();
  const int y = r.getScreenHeight() - 24 - paddingBottom;
  constexpr int inset = 12, batteryWidth = 26, batteryHeight = 14;
  const bool swap = SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
  char clock[12] = "--:--";
  halClock.formatTime(clock, sizeof(clock), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1);
  const int timeWidth = r.getTextWidth(SMALL_FONT_ID, clock);
  r.drawText(SMALL_FONT_ID, swap ? inset : width - inset - timeWidth, y, clock);
  const int by = y + 5;
  const int percent = std::max(0, std::min(100, static_cast<int>(powerManager.getBatteryPercentage())));
  char percentage[8];
  snprintf(percentage, sizeof(percentage), "%d", percent);
  const int batteryTextWidth = r.getTextWidth(SMALL_FONT_ID, percentage);
  const int bx = swap ? width - inset - batteryWidth - 7 - batteryTextWidth : inset;
  // Continuous fill, rounded silhouette, and an outline that stays visible at zero.
  r.drawLine(bx + 2, by, bx + batteryWidth - 4, by);
  r.drawLine(bx + 2, by + batteryHeight - 1, bx + batteryWidth - 4, by + batteryHeight - 1);
  r.drawLine(bx, by + 2, bx, by + batteryHeight - 3);
  r.drawLine(bx + batteryWidth - 2, by + 2, bx + batteryWidth - 2, by + batteryHeight - 3);
  r.drawPixel(bx + 1, by + 1);
  r.drawPixel(bx + 1, by + batteryHeight - 2);
  r.drawPixel(bx + batteryWidth - 3, by + 1);
  r.drawPixel(bx + batteryWidth - 3, by + batteryHeight - 2);
  r.fillRect(bx + batteryWidth - 1, by + 5, 2, 4);
  const int fill = ((batteryWidth - 5) * percent + 50) / 100;
  if (fill > 0) r.fillRect(bx + 2, by + 2, fill, batteryHeight - 4);
  r.drawText(SMALL_FONT_ID, bx + batteryWidth + 7, y, percentage);
  if (!title) return;
  const int left = (swap ? inset + timeWidth : bx + batteryWidth + 7 + batteryTextWidth) + 14;
  // Anchor the reader counters to the actual corner content, leaving one small gap.
  const int right = (swap ? bx : width - inset - timeWidth) - 14;
  char counts[48];
  snprintf(counts, sizeof(counts), "%s%d/%d %.0f%%", estimated ? "~" : "", currentPage, pageCount,
           std::max(0.0f, std::min(100.0f, bookProgress)));
  const int countWidth = r.getTextWidth(SMALL_FONT_ID, counts);
  const int countX = right - countWidth;
  r.drawText(SMALL_FONT_ID, countX, y, counts);
  const int markWidth = bookmarked ? 16 : 0;
  if (bookmarked) inlineSymbols::drawShape(r, inlineSymbols::Shape::Star, left + 6, y + 12, 10, true);
  const int room = std::max(0, countX - left - markWidth - 8);
  std::string name = r.truncatedText(SMALL_FONT_ID, title, std::max(0, room - r.getTextWidth(SMALL_FONT_ID, ":")));
  if (!name.empty()) name += ":";
  if (!name.empty()) r.drawText(SMALL_FONT_ID, left + markWidth, y, name.c_str());
}

int tenorchrome::tipY(const GfxRenderer& renderer) {
  return renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight - 26;
}
int tenorchrome::tipHeight(const GfxRenderer& renderer, const char* text, int maxLines) {
  constexpr int font = SMALL_FONT_ID;
  const auto lines = renderer.wrappedText(font, text, renderer.getScreenWidth() - 48, maxLines);
  return lines.empty() ? 0 : 28 + (static_cast<int>(lines.size()) - 1) * renderer.getLineHeight(font);
}
void tenorchrome::drawTip(const GfxRenderer& renderer, const char* text, int linesAbove, int maxLines) {
  constexpr int font = SMALL_FONT_ID;
  const auto lines = renderer.wrappedText(font, text, renderer.getScreenWidth() - 48, maxLines);
  int y = tipY(renderer) - (linesAbove + static_cast<int>(lines.size()) - 1) * renderer.getLineHeight(font);
  for (const auto& line : lines) {
    renderer.drawCenteredText(font, y, line.c_str());
    y += renderer.getLineHeight(font);
  }
}
