#pragma once
#include <HalGPIO.h>

#include "CrossPointSettings.h"
#include "UIScale.h"
class GfxRenderer;
namespace tenorchrome {
constexpr int HEADER_TOP = 5;
constexpr int HEADER_HEIGHT = 48;
constexpr int TAB_TOP = HEADER_TOP + HEADER_HEIGHT;
constexpr int TAB_HEIGHT = 59;
constexpr int CONTENT_TOP = TAB_TOP + TAB_HEIGHT + 16;
inline bool enabled() { return SETTINGS.uiTheme == CrossPointSettings::TENOR_UI && !gpio.hasTouch(); }
constexpr int STATUS_HEIGHT = 32;
constexpr int STATUS_TEXT_LANE = 24;
constexpr int STATUS_ICON_TOP_OFFSET = 5;
constexpr int STATUS_LARGE_TEXT_SHIFT = 10;
constexpr int STATUS_HEIGHT_LARGE = 40;
// Muc cua thanh trang thai nho bat dau cach mep duoi 22 px: chu ve o y = H - STATUS_TEXT_LANE va
// dinh muc cua font nho nam duoi y 2 px (do tren gia lap X3, ke ca dau chong nhu "Ậ").
constexpr int STATUS_INK_TOP = STATUS_TEXT_LANE - 2;
// Chu trong trinh doc duoc xuong toi cach muc thanh trang thai dung 2 px, do bang muc that cua dong
// (xem ChapterHtmlSlimParser::addLineToPage). Truoc day chua 28 px va do bang o dong nen khoang cach
// thuc te dao dong 1-9 px tuy muc gian dong, va co trang thieu 1 px la mat ca dong.
constexpr int READER_TEXT_TO_STATUS_GAP = 2;
constexpr int READER_BOTTOM_RESERVE = STATUS_INK_TOP + READER_TEXT_TO_STATUS_GAP;
inline int headerHeight() { return HEADER_HEIGHT + uiTextSizeSpec(SETTINGS.uiTextSize).bodyLineHeight - 33; }
inline int tabTop() { return HEADER_TOP + headerHeight(); }
inline int tabHeight() { return TAB_HEIGHT + uiTextSizeSpec(SETTINGS.uiTextSize).subtitleLineHeight - 26; }
inline int contentTop() { return tabTop() + tabHeight() + 16; }
inline int statusTextGrowth(bool large = false) {
  const auto text = uiTextSizeSpec(SETTINGS.uiTextSize);
  return large ? text.bodyLineHeight - 33 : text.captionLineHeight - 21;
}
inline int statusHeight(bool large = false) {
  return (large ? STATUS_HEIGHT_LARGE : STATUS_HEIGHT) + statusTextGrowth(large);
}
inline int readerBottomReserve() { return READER_BOTTOM_RESERVE + statusTextGrowth(); }
// First row of the reader's status bar ink; the page's text stops READER_TEXT_TO_STATUS_GAP above it.
inline int readerStatusTop(const int screenHeight) { return screenHeight - STATUS_INK_TOP - statusTextGrowth(); }
inline int statusTextY(const int screenHeight, const bool large, const int paddingBottom = 0) {
  return screenHeight - STATUS_TEXT_LANE - (large ? STATUS_LARGE_TEXT_SHIFT : 0) - paddingBottom - statusTextGrowth(large);
}
inline int statusIconTopY(const int screenHeight, const bool large, const int paddingBottom = 0) {
  return statusTextY(screenHeight, large, paddingBottom) + STATUS_ICON_TOP_OFFSET;
}
struct StatusCornerBounds {
  int leftEnd;
  int rightStart;
};
StatusCornerBounds statusCornerBounds(const GfxRenderer& renderer, bool large);
void drawStatus(const GfxRenderer& renderer, const char* title = nullptr, int currentPage = 0, int pageCount = 0,
                float bookProgress = 0, int paddingBottom = 0, bool estimated = false, bool bookmarked = false,
                bool titleIsName = true);
// Small symbols can start one pixel above the battery box (the Select check).
int smallFooterSymbolsTopY(const GfxRenderer& renderer);
bool compactFooterTips(bool hasTextHints = false);
// Callers with literal footer labels (including +/- or an unassigned key)
// retain the full text-hint reserve. Standard symbolic footers use the default.
int tipY(const GfxRenderer& renderer, bool hasTextHints = false);
int tipTopY(const GfxRenderer& renderer, const char* text, int linesAbove = 0, int maxLines = 4,
            bool hasTextHints = false);

// The one "more this way" mark: an open V, never filled (filled triangles are the physical
// buttons). `span` is half its opening across the pointing axis; the box it is drawn in is
// 2 * span + 1 across by moreChevronLength(span) along, (x, y) its top left corner.
enum class ChevronDir : uint8_t { Down, Left, Right };
constexpr int MORE_CHEVRON_STROKE = 3;
constexpr int moreChevronLength(const int span) { return (span * 8 + 5) / 11 + MORE_CHEVRON_STROKE; }
void drawMoreChevron(const GfxRenderer& renderer, int x, int y, ChevronDir dir, int span);

// Mui ten chu V o giua, ngay tren dong mach nuoc chan man: bao rang danh sach
// con dong ben duoi. Thay cho cau "1-10 / 13" o goc tren, vi it ai nhin thanh
// cuon va con so do lay mat cho cua ten the ben canh.
int moreBelowChevronTopY(const GfxRenderer& renderer, int hintTopY = -1);
void drawMoreBelowChevron(const GfxRenderer& renderer, int hintTopY = -1);
int tipLineCount(const GfxRenderer& renderer, const char* text, int maxLines = 4);
int tipHeight(const GfxRenderer& renderer, const char* text, int maxLines = 4);
void drawTip(const GfxRenderer& renderer, const char* text, int linesAbove = 0, int maxLines = 4,
             bool hasTextHints = false);
void drawHeader(const GfxRenderer& renderer, const char* title, const char* prefix = "");
void drawSiblingDestinations(const GfxRenderer& renderer, const char* previous, const char* next);
}  // namespace tenorchrome
