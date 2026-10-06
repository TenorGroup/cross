#pragma once
#include <BoardConfig.h>
#include <HalGPIO.h>

#include "CrossPointSettings.h"
#include "UIScale.h"
class GfxRenderer;
namespace tenorchrome {
constexpr int HEADER_TOP = 5;
constexpr int HEADER_HEIGHT = 48;
constexpr int TAB_TOP = HEADER_TOP + HEADER_HEIGHT;
constexpr int TAB_HEIGHT = 60;
constexpr int CONTENT_TOP = TAB_TOP + TAB_HEIGHT + 16;
// The X4 Pro draws every screen in tenor/cross like the button readers; it has no front buttons, so
// it shows no button hints or tips that name a button. Fixed at build time: the X3 and X4 builds
// compile the touch branches out.
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
constexpr bool kTouchShell = true;
#else
constexpr bool kTouchShell = false;
#endif
inline bool enabled() { return kTouchShell || !gpio.hasTouch(); }
// A tip that names a front button (one of the button symbols U+E100..U+E109) has nothing to point at
// on the touch shell.
inline bool tipShown(const char* text) {
  if (!kTouchShell || !text) return true;
  for (const char* c = text; c[0] && c[1] && c[2]; ++c)
    if (static_cast<unsigned char>(c[0]) == 0xEE && static_cast<unsigned char>(c[1]) == 0x84 &&
        static_cast<unsigned char>(c[2]) >= 0x80 && static_cast<unsigned char>(c[2]) <= 0x89)
      return false;
  return true;
}
constexpr int TOUCH_BAR_BOTTOM_GAP = 16;
// Dynamic bar (touch shell): every screen has one bar at the foot, 60 px tall and 16 px over the bottom
// edge, the same place and size on every screen; only what it holds changes. Each screen declares
// what it needs (footBarFor), and one function draws it and records where a tap on it goes:
//   Tabs: the zone's root, its cards drawn by the tab list itself (Home, the reader menu).
//   Full: a screen below another: "<" (one level back), the zone's round icon (to the zone's root)
//         and the screen's name, read only.
//   BackOnly: "<" alone, the screen's own content takes the rest of the foot (a keyboard, the reader
//             menu's cards).
// The rows above stop 8 px over it.
enum class FootBar : uint8_t { None, Tabs, Full, BackOnly };
// Where a screen belongs: the Home card it was reached from, or the book open under it.
enum class Zone : uint8_t { Recent, File, Favorites, Stats, Settings, Book };
FootBar footBarFor(const char* activityName);
inline bool wantsFootBack(const char* activityName) {
  const FootBar bar = footBarFor(activityName);
  return bar == FootBar::Full || bar == FootBar::BackOnly;
}
constexpr int FOOT_BACK_SIZE = 60;
constexpr int FOOT_BACK_X = 16;
constexpr int FOOT_PILL_GAP = 8;
inline int footBackTop(const int screenHeight) { return screenHeight - TOUCH_BAR_BOTTOM_GAP - FOOT_BACK_SIZE; }
inline int footBackReserve() { return TOUCH_BAR_BOTTOM_GAP + FOOT_BACK_SIZE + 8; }
// The title a screen's header names, kept for the bar's name pill (the touch shell draws no title row).
void noteScreenTitle(const char* title);
const char* screenTitle();
// A screen drawn on tenor/ugly paper with a foot of its own (a question sheet, the shell switch) says so as it
// draws: its bar is the "<" alone, in pen.
void noteHandDrawn();
// A short note for the middle of the status strip (the items of a folder), for the screen that set it.
void noteStatus(const char* text);
// True when the "B" of a remote linking should blink now: the caller repaints the screen (one refresh).
bool bluetoothBlinkDue();
void drawFootBar(const GfxRenderer& renderer, FootBar bar, Zone zone);
// A screen laid over a book's page clears the band of the bar first, so "<" stands on white, not on the
// page's last line.
void clearFootBand(const GfxRenderer& renderer);
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
// Reader view data for the existing pre-display hook. The reader owns its depth and drafts.
void noteReaderFootBar(bool open, bool keypad, int activeTool);
struct ReaderToolRect { int x, y, width, height; };
// Contents, Text, More and Favorites (founder 06/10).
constexpr int READER_TOOLS = 4;
inline ReaderToolRect readerToolRect(const int width, const int height, const int tool) {
  const int left = FOOT_BACK_X + FOOT_BACK_SIZE + FOOT_PILL_GAP;
  const int room = width - FOOT_BACK_X - left;
  const int x = left + room * tool / READER_TOOLS;
  return {x, footBackTop(height), left + room * (tool + 1) / READER_TOOLS - x, FOOT_BACK_SIZE};
}
#endif
// An icon in an icon bar (Mask1, bit 0 = ink): the chosen one solid, the others grey. Every icon bar draws
// its icons with this, so "not chosen" looks the same on every screen.
void drawBarIcon(const GfxRenderer& renderer, const uint8_t* bits, int w, int h, int x, int y, bool chosen);
// One tab of an icon bar whose band is h tall from y, centred on cx: the chosen one in a ring BAR_TAB_W wide,
// BAR_TAB_INSET in from the band's top and bottom (the Home bar's), its icon centred in the band.
// A pinned row's mark: the Favourites tab's heart, solid, FAVORITE_MARK px square, its top left at x, y.
constexpr int FAVORITE_MARK = 14;
void drawFavoriteMark(const GfxRenderer& renderer, int x, int y);
constexpr int BAR_TAB_W = 84;
constexpr int BAR_TAB_INSET = 6;
void drawBarTab(const GfxRenderer& renderer, int cx, int y, int h, const uint8_t* bits, int w, int iconH, bool chosen);
// A ring of `thick` px just inside a round-ended bar; grey = every other pixel.
void drawPillRing(const GfxRenderer& g, int x, int y, int w, int h, int thick, bool grey);
// The same ring inside a box with corners of radius r (a row menu, a group of list rows).
void drawRoundRing(const GfxRenderer& g, int x, int y, int w, int h, int r, int thick, bool grey);
// Touch shell: the one frame around a group of content across the screen, FOOT_BACK_X in from both sides,
// grey dots 2 px, radius PANEL_RADIUS. Text inside starts 16 px in from it.
constexpr int PANEL_RADIUS = 20;
void drawPanel(const GfxRenderer& g, int y, int h);
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
// Touch: the header row is the status strip alone (clock, a note, radio icons, battery); the title went
// to the bar at the foot and the tab band is there too.
constexpr int TOUCH_STATUS_TOP_INSET = 8;
constexpr int TOUCH_STRIP_HEIGHT = 24 + TOUCH_STATUS_TOP_INSET;
inline int headerHeight() {
  return kTouchShell ? TOUCH_STRIP_HEIGHT - TOUCH_STATUS_TOP_INSET - HEADER_TOP
                     : HEADER_HEIGHT + uiTextSizeSpec(SETTINGS.uiTextSize).bodyLineHeight - 33;
}
inline int tabTop() { return HEADER_TOP + headerHeight() + (kTouchShell ? TOUCH_STATUS_TOP_INSET : 0); }
inline int tabHeight() { return TAB_HEIGHT + uiTextSizeSpec(SETTINGS.uiTextSize).subtitleLineHeight - 26; }
inline int contentTop() { return kTouchShell ? TOUCH_STRIP_HEIGHT + 6 : tabTop() + tabHeight() + 16; }
// Touch shell: the tab bar sits at the foot of the screen, 16 px above the Home key under the glass,
// and what the button readers put under their tab band moves up by the band.
inline int touchBarTop(const int screenHeight) { return screenHeight - TOUCH_BAR_BOTTOM_GAP - tabHeight(); }
inline int contentTopUnderTabs() { return contentTop(); }
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
// Touch: the grey ">" at the end of a list row that opens a deeper screen, its box (x, y) as above.
constexpr int ROW_CHEVRON_SPAN = 7;
void drawRowChevron(const GfxRenderer& renderer, int x, int y);

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
