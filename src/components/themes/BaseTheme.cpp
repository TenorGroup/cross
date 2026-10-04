#include "BaseTheme.h"
#include "ClockStatus.h"
#include "components/UIScale.h"

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "I18n.h"
#include "RecentBooksStore.h"
#include "components/TenorMenuChrome.h"
#include "components/HeaderBackTapTarget.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "components/icons/bookmark.h"
#include "TenorRadius.h"
#include "components/icons/cover.h"
#include "components/icons/headerIcons.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int homeMenuMargin = 20;
constexpr int homeMarginTop = 30;
constexpr int subtitleY = 738;
constexpr int bookmarkStatusIconWidth = 16;
constexpr int bookmarkStatusIconHeight = 14;
constexpr int bookmarkStatusIconGap = 4;
constexpr int bookmarkStatusIconTopCrop = 2;

void drawBookmarkStatusIcon(const GfxRenderer& renderer, const int x, const int y) {
  constexpr int bytesPerRow = bookmarkStatusIconWidth / 8;
  for (int row = 0; row < bookmarkStatusIconHeight; ++row) {
    for (int col = 0; col < bookmarkStatusIconWidth; ++col) {
      const uint8_t byte = BookmarkStatusIcon[(row + bookmarkStatusIconTopCrop) * bytesPerRow + col / 8];
      const uint8_t mask = 1U << (7 - (col % 8));
      renderer.drawPixel(x + col, y + row, (byte & mask) != 0);
    }
  }
}

}  // namespace

void BaseTheme::drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight) {
  // Top line
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  // Bottom line
  renderer.drawLine(x + 1, y + rectHeight - 1, x + battWidth - 3, y + rectHeight - 1);
  // Left line
  renderer.drawLine(x, y + 1, x, y + rectHeight - 2);
  // Battery end
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rectHeight - 2);
  renderer.drawPixel(x + battWidth - 1, y + 3);
  renderer.drawPixel(x + battWidth - 1, y + rectHeight - 4);
  renderer.drawLine(x + battWidth - 0, y + 4, x + battWidth - 0, y + rectHeight - 5);
}

void BaseTheme::drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY) {
  // Draw lightning bolt (white/inverted on black fill for visibility)
  renderer.drawLine(boltX + 4, boltY + 0, boltX + 5, boltY + 0, false);
  renderer.drawLine(boltX + 3, boltY + 1, boltX + 4, boltY + 1, false);
  renderer.drawLine(boltX + 2, boltY + 2, boltX + 5, boltY + 2, false);
  renderer.drawLine(boltX + 3, boltY + 3, boltX + 4, boltY + 3, false);
  renderer.drawLine(boltX + 2, boltY + 4, boltX + 3, boltY + 4, false);
  renderer.drawLine(boltX + 1, boltY + 5, boltX + 4, boltY + 5, false);
  renderer.drawLine(boltX + 2, boltY + 6, boltX + 3, boltY + 6, false);
  renderer.drawLine(boltX + 1, boltY + 7, boltX + 2, boltY + 7, false);
}

void BaseTheme::drawBatteryLeft(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Left aligned: icon on left, percentage on right (reader mode)
  const uint16_t percentage = powerManager.getDisplayedBatteryPercentage();
  const int y = rect.y + 6;

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    renderer.drawText(SMALL_FONT_ID, rect.x + batteryPercentSpacing + rect.width, rect.y, percentageText.c_str());
  }

  const Rect iconRect{rect.x, y, rect.width, rect.height};
  drawBatteryOutline(renderer, rect.x, y, rect.width, rect.height);
  fillBatteryIcon(renderer, iconRect, percentage);
}

void BaseTheme::drawCoverPlaceholder(const GfxRenderer& renderer, Rect rect) {
  if (rect.width <= 0 || rect.height <= 0) return;
  const int topHeight = rect.height / 3;
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.fillRect(rect.x, rect.y + topHeight, rect.width, rect.height - topHeight, true);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);
  constexpr int ICON_SIZE = 32;
  if (rect.width >= ICON_SIZE + 4 && topHeight >= ICON_SIZE + 4) {
    const int insetX = std::min(24, (rect.width - ICON_SIZE) / 2);
    const int insetY = std::min(24, (topHeight - ICON_SIZE) / 2);
    renderer.drawIcon(CoverIcon, rect.x + insetX, rect.y + insetY, ICON_SIZE);
  }
}

bool BaseTheme::drawCoverThumbFill(const GfxRenderer& renderer, const Bitmap& bitmap, Rect slot, const int xOffset) {
  if (slot.width <= 0 || slot.height <= 0) return false;
  // xOffset nudges the centered art sideways; the clip stays on the slot.
  const int x = slot.x + (slot.width - bitmap.getWidth()) / 2 + xOffset;
  const int y = slot.y + (slot.height - bitmap.getHeight()) / 2;
  const auto clip = renderer.getClipRect();
  const int left = std::max(slot.x, clip[0]);
  const int top = std::max(slot.y, clip[1]);
  renderer.setClipRect(left, top, std::max(0, std::min(slot.x + slot.width, clip[0] + clip[2]) - left),
                       std::max(0, std::min(slot.y + slot.height, clip[1] + clip[3]) - top));
  const bool drawn = renderer.drawBitmap(bitmap, x, y, bitmap.getWidth(), bitmap.getHeight());
  renderer.setClipRect(clip[0], clip[1], clip[2], clip[3]);
  return drawn;
}

void BaseTheme::drawProgressBar(const GfxRenderer& renderer, Rect rect, const size_t current, const size_t total) {
  if (total == 0) {
    return;
  }

  // Use 64-bit arithmetic to avoid overflow for large files
  const int percent = static_cast<int>((static_cast<uint64_t>(current) * 100) / total);

  LOG_DBG("UI", "Drawing progress bar: current=%u, total=%u, percent=%d", current, total, percent);
  // Draw outline
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  // Draw filled portion
  const int fillWidth = (rect.width - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, rect.height - 4);
  }

  // Draw percentage text centered below bar
  const std::string percentText = std::to_string(percent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, rect.y + rect.height + 15, percentText.c_str());
}

// Centre a button-hint label inside its box. A label that fits is drawn on the
// single baseline it always was; one too wide used to overflow the button border
// and run into the neighbouring hint, and now wraps to at most two centred lines
// (wrappedText() ellipsises anything that still doesn't fit). Shared so every
// theme's drawButtonHints() gets the same behaviour.
void BaseTheme::drawHintLabel(const GfxRenderer& renderer, const int fontId, const char* label, const int x,
                              const int boxWidth, const int boxTop, const int boxHeight, const int singleLineYOffset) {
  constexpr int textPadding = 3;  // keeps a wrapped label off the button's border
  const int maxTextWidth = boxWidth - (textPadding * 2);

  const bool small = normalizedUiTextSize(SETTINGS.uiTextSize) == 0;
  const int textWidth = renderer.getTextWidth(fontId, label);
  if (textWidth <= maxTextWidth) {
    const int offset = small ? singleLineYOffset : std::max(1, (boxHeight - renderer.getLineHeight(fontId)) / 2);
    renderer.drawText(fontId, x + (boxWidth - 1 - textWidth) / 2, boxTop + offset, label);
    return;
  }

  const auto lines = renderer.wrappedText(fontId, label, maxTextWidth, 2);
  if (small && !lines.empty()) {
    // The small Tenor footer has 35px: two ascender-based rows can put the
    // lower dot of "nhat" below the panel. Pack the actual ink, retaining a
    // 1px row gap when the usual 2px gap would exceed the available box.
    int tops[2] = {}, heights[2] = {};
    int inkHeight = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
      tops[i] = renderer.getTextInkTop(fontId, lines[i].c_str(), EpdFontFamily::REGULAR);
      heights[i] = renderer.getTextInkBottom(fontId, lines[i].c_str(), EpdFontFamily::REGULAR) - tops[i];
      inkHeight += heights[i];
    }
    const int gaps = static_cast<int>(lines.size()) - 1;
    if (inkHeight + gaps > boxHeight) {
      // Two full glyph rows physically do not fit. Keep the active font and
      // expose truncation with an ellipsis, instead of losing lower accents.
      const auto compact = renderer.truncatedText(fontId, label, maxTextWidth, EpdFontFamily::REGULAR);
      const int top = renderer.getTextInkTop(fontId, compact.c_str(), EpdFontFamily::REGULAR);
      const int height = renderer.getTextInkBottom(fontId, compact.c_str(), EpdFontFamily::REGULAR) - top;
      const int y = boxTop + std::max(0, (boxHeight - height) / 2) - top;
      renderer.drawText(fontId, x + (boxWidth - 1 - renderer.getTextWidth(fontId, compact.c_str())) / 2,
                        y, compact.c_str());
      return;
    }
    const int gap = gaps ? std::min(2, (boxHeight - inkHeight) / gaps) : 0;
    int inkY = boxTop + (boxHeight - inkHeight - gap * gaps) / 2;
    for (size_t i = 0; i < lines.size(); ++i) {
      const int lineWidth = renderer.getTextWidth(fontId, lines[i].c_str());
      renderer.drawText(fontId, x + (boxWidth - 1 - lineWidth) / 2, inkY - tops[i], lines[i].c_str());
      inkY += heights[i] + gap;
    }
    return;
  }

  // Enlarged hints have room for complete line boxes, accents and leading.
  constexpr int lineGap = 2;
  const int step = renderer.getLineHeight(fontId) + lineGap;
  const int block = static_cast<int>(lines.size()) * step - lineGap;
  int lineY = boxTop + std::max(1, (boxHeight - block) / 2);
  for (const auto& line : lines) {
    const int lineWidth = renderer.getTextWidth(fontId, line.c_str());
    renderer.drawText(fontId, x + (boxWidth - 1 - lineWidth) / 2, lineY, line.c_str());
    lineY += step;
  }
}

// Slightly inside the side padding: the battery's boxed glyph and the clock
// digits read wider than text/cover ink on the same line, so flush placement
// looks like it overhangs the content columns.
int BaseTheme::headerStatusInset() { return UITheme::getInstance().getMetrics().headerSidePadding + 4; }

void BaseTheme::applyHeaderStatus(const GfxRenderer& renderer, freeink::ui::HeaderProps& props) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  auto& status = props.status;

  // Status text stays at the fixed small font on every screen: FONT_LABEL is
  // bound to SMALL_FONT_ID by makeUiTarget() (FUI screens) and drawHeader()
  // (passive frames), while the uiScale FONT_SMALL is for list subtitles.
  status.battery.text.font = freeink::ui::GfxRendererTarget::FONT_LABEL;

  // Hiding the global status bar hides the header battery too (tenor/cross setting).
  status.showBattery = !SETTINGS.hideGlobalStatusBar;
  const uint16_t percentage = powerManager.getDisplayedBatteryPercentage();
  status.battery.percent = static_cast<uint8_t>(percentage > 100 ? 100 : percentage);
  status.battery.charging = gpio.isUsbConnected();
  // Static label buffers: headers draw on the single render task, and the
  // strings only need to outlive the fui::header() call that consumes them.
  static char percentText[8];
  if (SETTINGS.batteryPercentShown(false)) {
    snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(percentage));
    status.battery.label = percentText;
  }
  status.battery.glyphWidth = static_cast<int16_t>(metrics.batteryWidth);
  status.battery.glyphHeight = static_cast<int16_t>(metrics.batteryHeight);
  status.battery.gap = batteryPercentSpacing;
  status.batteryLeft = metrics.headerBatterySide == 1;
  status.edgeInset = static_cast<int16_t>(headerStatusInset());

  // Header chrome geometry. Status lives on the theme's thin top strip in
  // fixed corners - battery top-right, a corner clock (headerClockCentered =
  // false) top-left, a centered clock top-center - and never repositions.
  // The content row (title, back arrow, trailing action buttons) centers on
  // the region between the strip and the band bottom, so its padding reads
  // as balanced under the status line rather than against the full band.
  // Text hangs low in its line cell by the font's internal leading; the icon
  // buttons drop by that amount to align with the glyphs the user sees.
  // Buttons keep a standard square (a band-8 button dwarfs its 24px icon);
  // the boxes are invisible and minTouchSize pads the tap target.
  constexpr int16_t headerButtonSize = 48;
  const int16_t bandHeight = static_cast<int16_t>(metrics.headerHeight);
  const int16_t strip = static_cast<int16_t>(metrics.batteryBarHeight);
  const int titleFontId = uiScaleSpec().titleFontId;
  const int16_t opticalDrop =
      static_cast<int16_t>((renderer.getLineHeight(titleFontId) - renderer.getTextHeight(titleFontId)) / 2);
  props.leadingSize = headerButtonSize;
  props.trailingSize = headerButtonSize;
  props.actionOffsetY = static_cast<int16_t>(strip + (bandHeight - strip - headerButtonSize) / 2 - 4 + opticalDrop);
  // Shift the title's band-centered box down by half the strip: its center
  // lands on the below-strip region's midline with the buttons.
  props.titleOffsetY = static_cast<int16_t>(strip / 2);
  status.stripHeight = strip;
  status.clockCentered = metrics.headerClockCentered;

  // Header clock, opposite the battery, on every screen that draws this band
  // (SETTINGS.clockShowInHeader). Themes whose title layout has no room for
  // the clock's left reserve opt out via headerShowsClock.
  static char clockText[10];
  // Same rule as the status bar clock: no time is shown before the clock holds a real date.
  if (metrics.headerShowsClock && SETTINGS.clockShowInHeader && clockstatus::hasValidTime() &&
      halClock.formatTime(clockText, sizeof(clockText), SETTINGS.clockFormat == 1)) {
    status.clockText = clockText;
  }
}

void BaseTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle,
                           const bool backButton) const {
  // Every activity header renders through the FreeInkUI header + battery
  // indicator components, styled by the active theme's tokens (padding,
  // centering, underline). Non-interactive frame: no hit rects registered.
  namespace fui = freeink::ui;
  const auto spec = uiScaleSpec();
  fui::GfxRendererFrame<1> ui(renderer, spec.smallFontId, spec.bodyFontId, spec.titleFontId);
  // Refresh the app-wide shared tokens instead of copying ~1.5KB of
  // ThemeTokens onto this render-path stack frame; the values derived here
  // are identical to what every FreeInkApp screen derives. Goes through the
  // same publish-a-fresh-slot path applySharedUiTheme() uses (see
  // UiAppHelpers.h) rather than overwriting the previously-published
  // instance in place, since some other FreeInkApp could be mid-read of it.
  const fui::ThemeTokens& tokens = refreshSharedUiThemeTokens(ui.target);
  // Header status text (battery percent, right label) stays at the fixed
  // small font like the legacy headers; the uiScale small font is for list
  // subtitles.
  ui.target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  ui.target.setFont(fui::GfxRendererTarget::FONT_LABEL, SMALL_FONT_ID);
  const fui::Rect band{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
                       static_cast<int16_t>(rect.height)};

  fui::HeaderProps props;
  props.title = title;
  props.rightLabel = subtitle;  // firmware headers right-align the secondary text
  // Battery + clock chrome and their title reserves live in the FreeInkUI
  // header component; this only fills the values from settings and metrics.
  applyHeaderStatus(renderer, props);
  if (rect.height < UITheme::getInstance().getMetrics().headerHeight) {
    // Short bands (home) are not split into strip + content row: the title
    // centers on the band, clear of the band's bottom edge.
    props.titleOffsetY = 0;
  }
  // Tappable back button leading the band on touch boards, so every pushed
  // screen offers a visible way out beside the edge-swipe gesture. This frame
  // registers no hit rects, so the rect is recorded in HeaderBackTapTarget and
  // MappedInputManager folds taps on it into Button::Back. Same geometry as
  // the FUI header's leading slot (applyHeaderStatus set the size/offset) so
  // the recorded rect matches the drawn button.
  const int16_t backBtnSize = props.leadingSize;
  const bool showBackButton = backButton && title != nullptr && gpio.hasTouch();
  if (showBackButton) {
    props.leadingIcon = fui::bitmapFromIcon(icon_header_back_32);
    props.leadingAction = 1;  // any non-NO_ACTION id: paints the button, routing is via HeaderBackTapTarget
    HeaderBackTapTarget::set(band.x + 4, band.y + 4 + props.actionOffsetY, backBtnSize, backBtnSize);
  } else {
    HeaderBackTapTarget::clear();
  }
  props.borderEdges = fui::EdgeBottom;
  props.titleText = tokens.titleText;
  props.titleText.align = tokens.headerTitleAlign;
  props.subtitleText = tokens.smallText;
  props.styles = tokens.popup;
  props.sidePadding = tokens.headerSidePadding;
  // Underline only under a titled header: an untitled band (Lyra home screen)
  // historically drew no rule, and the old themes keyed the line on the title.
  if (title != nullptr && props.styles.normal.border.kind == fui::PaintKind::None && tokens.headerUnderline > 0) {
    props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    props.styles.normal.borderWidth = tokens.headerUnderline;
  }
  fui::header(ui.frame, band, props);
}

int BaseTheme::getMenuRowHeight(const GfxRenderer&) const { return UITheme::getInstance().getMetrics().menuRowHeight; }

Rect BaseTheme::drawPopup(const GfxRenderer& renderer, const char* message, const bool display) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int marginX = metrics.popupMarginX;
  const int marginY = metrics.popupMarginY;
  const int frameThickness = metrics.popupFrameThickness;
  const EpdFontFamily::Style popupFontFamily = metrics.popupTextBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  // Scale y position proportionally to screen height
  const int y = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, popupFontFamily);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + marginX * 2;
  const int h = textHeight + marginY * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  const bool useRoundedPopup = metrics.popupCornerRadius > 0;
  if (useRoundedPopup) {
    // A one-line notice is a leaf of its own height, framed concentrically: the sheet radius
    // (28) on a 57 px block made a pill.
    const int radius = metrics.roundedMarks ? tenorradius::leaf(h) : metrics.popupCornerRadius;
    renderer.fillRoundedRect(x - frameThickness, y - frameThickness, w + frameThickness * 2, h + frameThickness * 2,
                             tenorradius::container(radius, frameThickness), Color::White);
    renderer.fillRoundedRect(x, y, w, h, radius, Color::Black);
  } else {
    renderer.fillRect(x - frameThickness, y - frameThickness, w + frameThickness * 2, h + frameThickness * 2, true);
    renderer.fillRect(x, y, w, h, false);
  }

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + marginY + metrics.popupTextBaselineOffsetY;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, metrics.popupTextInverted, popupFontFamily);
  if (display) renderer.displayBuffer();
  return Rect{x, y, w, h};
}

void BaseTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int barHeight = metrics.popupProgressBarHeight;
  const int barWidth =
      std::max(0, layout.width - metrics.popupMarginX * 2);  // twice the margin in drawPopup to match text width
  const int barX = layout.x + (layout.width - barWidth) / 2;
  const int barY = layout.y + layout.height - metrics.popupMarginY / 2 - barHeight / 2 - 1;
  if (barWidth <= 0 || barHeight <= 0) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  const int scaledProgress = metrics.popupProgressClampPercent ? std::clamp(progress, 0, 100) : progress;
  const int fillWidth = barWidth * scaledProgress / 100;

  if (metrics.popupProgressDrawOutline) {
    renderer.drawRect(barX, barY, barWidth, barHeight, 1, metrics.popupProgressOutlineInverted);
  }
  if (fillWidth > 0) {
    renderer.fillRect(barX, barY, fillWidth, barHeight, metrics.popupProgressFillInverted);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void BaseTheme::drawStatusBar(GfxRenderer& renderer, const float bookProgress, const int currentPage,
                              const int pageCount, std::string title, const int paddingBottom, const int textYOffset,
                              const bool fillMargin, const bool isPageBookmarked, const bool pageCountEstimated,
                              const bool titleIsName) {
  if (SETTINGS.readerStatusBarHidden()) return;
  if (tenorchrome::enabled()) {
    tenorchrome::drawStatus(renderer, title.c_str(), currentPage, pageCount, bookProgress, paddingBottom,
                            pageCountEstimated, isPageBookmarked, titleIsName);
    return;
  }
  auto metrics = UITheme::getInstance().getMetrics();
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  const auto sb = SETTINGS.statusBarSpec();
  const bool showStatusBarTextLane = sb.textLaneVisible(clockstatus::hasValidTime());

  // Draw Progress Text
  const auto screenHeight = renderer.getScreenHeight();
  auto textY = screenHeight - UITheme::getInstance().getStatusBarHeight() - orientedMarginBottom - paddingBottom - 4;

  const int leftClusterX = metrics.statusBarHorizontalMargin + orientedMarginLeft + 1;
  const int rightClusterX = renderer.getScreenWidth() - metrics.statusBarHorizontalMargin - orientedMarginRight;
  int leftClusterWidth = 0;
  int rightClusterWidth = 0;

  // A negative bookProgress is unknown (a book still building its index): no percent, no book bar.
  const bool showBookPercent = sb.showBookProgressPercent && bookProgress >= 0;
  if (showBookPercent || sb.showChapterPageCount) {
    // Right aligned text for progress counter
    char progressStr[32];

    // Draw the estimate marker separately so it can use the next UI font size.
    const bool showEstimate = pageCountEstimated && sb.showChapterPageCount;

    if (showBookPercent && sb.showChapterPageCount) {
      snprintf(progressStr, sizeof(progressStr), "%d/%d  %.0f%%", currentPage, pageCount, bookProgress);
    } else if (showBookPercent) {
      snprintf(progressStr, sizeof(progressStr), "%.0f%%", bookProgress);
    } else {
      snprintf(progressStr, sizeof(progressStr), "%d/%d", currentPage, pageCount);
    }

    int progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressStr);
    const int estimateWidth = showEstimate ? renderer.getTextWidth(UI_10_FONT_ID, "~") : 0;
    constexpr int estimateGap = 2;
    const int estimateSpacing = showEstimate ? estimateGap : 0;
    const int progressX = rightClusterX - estimateWidth - estimateSpacing - progressTextWidth;
    if (showEstimate) {
      const int estimateY = textY + (renderer.getLineHeight(SMALL_FONT_ID) - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
      renderer.drawText(UI_10_FONT_ID, progressX, estimateY, "~");
    }
    renderer.drawText(SMALL_FONT_ID, progressX + estimateWidth + estimateSpacing, textY, progressStr);

    rightClusterWidth += estimateWidth + estimateSpacing + progressTextWidth;
  }

  // Draw Progress Bar
  if (sb.showsProgressBar()) {
    const int barMarginLeft = fillMargin ? 0 : orientedMarginLeft;
    const int barMarginRight = fillMargin ? 0 : orientedMarginRight;
    const int progressBarMaxWidth = renderer.getScreenWidth() - barMarginLeft - barMarginRight;
    const int progressBarY = renderer.getScreenHeight() - orientedMarginBottom - sb.progressBarHeightPx -
                             paddingBottom + (fillMargin ? 1 : 0);
    size_t progress;
    if (sb.progressBarMode == CrossPointSettings::STATUS_BAR_PROGRESS_BAR::BOOK_PROGRESS) {
      progress = bookProgress > 0 ? static_cast<size_t>(bookProgress) : 0;
    } else {
      // Chapter progress
      progress = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) * 100 : 0;
    }
    const int barWidth = progressBarMaxWidth * progress / 100;
    const int barHeight = sb.progressBarHeightPx + (fillMargin ? orientedMarginBottom - 1 : 0);
    renderer.fillRect(barMarginLeft, progressBarY, barWidth, barHeight, true);
  }

  // Draw Battery
  const bool showBatteryPercentage = sb.showBatteryPercent;

  if (sb.showBattery) {
    GUI.drawBatteryLeft(renderer,
                        Rect{leftClusterX + leftClusterWidth, textY, metrics.batteryWidth, metrics.batteryHeight},
                        showBatteryPercentage);
    int batteryWidth = metrics.batteryWidth;

    if (showBatteryPercentage) {
      const uint16_t percentage = powerManager.getDisplayedBatteryPercentage();
      // width of icon + spacing + text for layout purposes
      batteryWidth +=
          batteryPercentSpacing + renderer.getTextWidth(SMALL_FONT_ID, (std::to_string(percentage) + "%").c_str());
    }

    leftClusterWidth += batteryWidth;
  }

  // Read the clock only as part of an existing page/status-bar render.
  // A minute change must not schedule its own e-paper refresh.
  if (sb.showsClock() && clockstatus::hasValidTime()) {
    char timeBuf[9];
    if (halClock.formatTime(timeBuf, sizeof(timeBuf), sb.clock12h)) {
      int clockTextWidth = renderer.getTextWidth(SMALL_FONT_ID, timeBuf);
      int clockX = 0;
      // Position to the left or right of the progress text (with a small gap)
      if (sb.clockMode == CrossPointSettings::STATUS_BAR_CLOCK_LEFT) {
        clockX = leftClusterX + leftClusterWidth + (leftClusterWidth > 0 ? 10 : 0);
        leftClusterWidth += clockTextWidth + 10;
      } else if (sb.clockMode == CrossPointSettings::STATUS_BAR_CLOCK_RIGHT) {
        clockX = rightClusterX - rightClusterWidth - (rightClusterWidth > 0 ? 10 : 0) - clockTextWidth;
        rightClusterWidth += clockTextWidth + 10;
      }
      renderer.drawText(SMALL_FONT_ID, clockX, textY, timeBuf);
    }
  }

  // Draw Bookmark
  if (showStatusBarTextLane && isPageBookmarked) {
    const int bookmarkGap = leftClusterWidth > 0 ? bookmarkStatusIconGap : 0;
    const int bookmarkX = leftClusterX + leftClusterWidth + bookmarkGap;
    const int bookmarkY = textY + 5;
    drawBookmarkStatusIcon(renderer, bookmarkX, bookmarkY);
    leftClusterWidth += bookmarkStatusIconWidth + bookmarkGap;
  }

  // Draw Title
  if (!title.empty()) {
    textY -= textYOffset;
    // Centered chapter title text
    // Page width minus existing content with 30px padding on each side
    const int rendererableScreenWidth =
        renderer.getScreenWidth() - (metrics.statusBarHorizontalMargin * 2) - orientedMarginLeft - orientedMarginRight;

    const int titleMarginLeft = leftClusterWidth + 30;
    const int titleMarginRight = rightClusterWidth + 30;

    // Attempt to center title on the screen, but if title is too wide then later we will center it within the
    // available space.
    int titleMarginLeftAdjusted = std::max(titleMarginLeft, titleMarginRight);
    int availableTitleSpace = rendererableScreenWidth - 2 * titleMarginLeftAdjusted;

    int titleWidth;
    titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    if (titleWidth > availableTitleSpace) {
      // Not enough space to center on the screen, center it within the remaining space instead
      availableTitleSpace = rendererableScreenWidth - titleMarginLeft - titleMarginRight;
      titleMarginLeftAdjusted = titleMarginLeft;
    }
    if (titleWidth > availableTitleSpace) {
      title = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), availableTitleSpace);
      titleWidth = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    }

    renderer.drawText(SMALL_FONT_ID,
                      titleMarginLeftAdjusted + metrics.statusBarHorizontalMargin + orientedMarginLeft +
                          (availableTitleSpace - titleWidth) / 2,
                      textY, title.c_str());
  }
}

void BaseTheme::drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  auto truncatedLabel =
      renderer.truncatedText(SMALL_FONT_ID, label, rect.width - metrics.contentSidePadding * 2, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, rect.y, truncatedLabel.c_str());
}

void BaseTheme::drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode,
                              int contentStartX, int contentWidth) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineY = rect.y + rect.height + lineHeight + metrics.verticalSpacing;
  const int thickness = cursorMode ? metrics.textFieldCursorThickness : metrics.textFieldNormalThickness;
  if (contentWidth > 0) {
    renderer.drawLine(rect.x + contentStartX, lineY,
                      rect.x + contentStartX + contentWidth + metrics.textFieldLineEndOffset, lineY, thickness, true);
  } else {
    const int lineW = textWidth + metrics.textFieldHorizontalPadding * 2;
    const int lineStart = rect.x + (rect.width - lineW) / 2;
    renderer.drawLine(lineStart, lineY, lineStart + lineW + metrics.textFieldLineEndOffset, lineY, thickness, true);
  }
}
