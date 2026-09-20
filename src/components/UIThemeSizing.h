#pragma once

#include <algorithm>

#include "UIScale.h"
#include "themes/BaseTheme.h"

// Keep the Small layout byte-for-byte compatible. Larger tiers reserve complete
// line boxes plus the existing theme's spacing. Bitmap sizes stay independent.
inline ThemeMetrics uiSizedThemeMetrics(ThemeMetrics metrics, uint8_t tier) {
  if (normalizedUiTextSize(tier) == 0) return metrics;
  const auto text = uiTextSizeSpec(tier);
  const int captionDelta = text.captionLineHeight - 21;
  const int subtitleDelta = text.subtitleLineHeight - 26;
  const int bodyDelta = text.bodyLineHeight - 33;
  metrics.batteryBarHeight += captionDelta;
  metrics.headerHeight += bodyDelta + (metrics.headerBatteryDetached ? captionDelta : 0);
  metrics.listRowHeight = std::max(metrics.listRowHeight + bodyDelta, text.bodyLineHeight + 8);
  metrics.listWithSubtitleRowHeight = std::max(metrics.listWithSubtitleRowHeight + bodyDelta + subtitleDelta,
                                             text.bodyLineHeight + text.subtitleLineHeight + 8);
  metrics.menuRowHeight = std::max(metrics.menuRowHeight + bodyDelta, text.bodyLineHeight + 8);
  metrics.tabBarHeight += subtitleDelta;
  metrics.buttonHintsHeight = std::max(metrics.buttonHintsHeight + subtitleDelta, 2 * text.subtitleLineHeight + 14);
  metrics.sideButtonHintsWidth += captionDelta;
  metrics.statusBarVerticalMargin += captionDelta;
  metrics.keyboardKeyHeight = std::max(metrics.keyboardKeyHeight + bodyDelta, text.bodyLineHeight + 8);
  return metrics;
}
