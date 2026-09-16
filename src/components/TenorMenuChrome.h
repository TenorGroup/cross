#pragma once
#include <HalGPIO.h>

#include "CrossPointSettings.h"
class GfxRenderer;
namespace tenorchrome {
constexpr int HEADER_TOP = 5;
constexpr int HEADER_HEIGHT = 48;
constexpr int TAB_TOP = HEADER_TOP + HEADER_HEIGHT;
constexpr int TAB_HEIGHT = 59;
constexpr int CONTENT_TOP = TAB_TOP + TAB_HEIGHT + 16;
inline bool enabled() { return SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && !gpio.hasTouch(); }
constexpr int STATUS_HEIGHT = 32;
void drawStatus(const GfxRenderer& renderer, const char* title = nullptr, int currentPage = 0, int pageCount = 0,
                float bookProgress = 0, int paddingBottom = 0, bool estimated = false, bool bookmarked = false);
int tipY(const GfxRenderer& renderer);
int tipHeight(const GfxRenderer& renderer, const char* text, int maxLines = 4);
void drawTip(const GfxRenderer& renderer, const char* text, int linesAbove = 0, int maxLines = 4);
void drawHeader(const GfxRenderer& renderer, const char* title, const char* prefix = "");
}  // namespace tenorchrome
