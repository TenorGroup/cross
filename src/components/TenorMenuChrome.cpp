#include "TenorMenuChrome.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>

#include <algorithm>
#include <iterator>
#include <cstdio>
#include <string>
#include <vector>

#include "ButtonSymbols.h"
#include "UITheme.h"
#include "fontIds.h"
#include "themes/TenorRadius.h"

void tenorchrome::drawHeader(const GfxRenderer& r, const char* title, const char* prefix) {
  constexpr int x = 18, rightReserve = 18, tracking = 1;
  constexpr int font = UI_12_FONT_ID;
  constexpr auto dir = BidiUtils::BidiBaseDir::AUTO;
  const int width = std::max(1, r.getScreenWidth() - x - rightReserve);
  const int y = HEADER_TOP + (headerHeight() - r.getLineHeight(font)) / 2;
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

namespace {
// The list's down chevron: 23 px across.
constexpr int MORE_BELOW_SPAN = 11;
// Khoang giua bieu tuong pin va chu so phan tram o ca hai chieu sap xep.
constexpr int BATTERY_TEXT_GAP = 4;
constexpr int STATUS_CORNER_INSET = 8;
// Half the list's down chevron, 15 px tall beside a 26 px label.
constexpr int SIBLING_CHEVRON_SPAN = 7;
constexpr int SIBLING_CHEVRON_WIDTH = tenorchrome::moreChevronLength(SIBLING_CHEVRON_SPAN);
constexpr int SIBLING_CHEVRON_HEIGHT = 2 * SIBLING_CHEVRON_SPAN + 1;
constexpr int SIBLING_EDGE = 18;
constexpr int SIBLING_LABEL_GAP = 6;
constexpr int SIBLING_CENTER_GAP = 16;

// Charging bolt drawn alone in the battery body, one row per entry, bit 0 = leftmost pixel.
// Drawn by hand on the pixel grid: a 3 px stroke, two rows of crossbar, the tips kept.
constexpr uint16_t BOLT_SMALL[] = {0x070, 0x038, 0x01C, 0x00E, 0x0FF, 0x0FF, 0x038, 0x01C, 0x00E, 0x006};
constexpr int BOLT_SMALL_WIDTH = 8;
constexpr uint16_t BOLT_LARGE[] = {0x380, 0x1C0, 0x0E0, 0x070, 0x038, 0x01C, 0x3FF,
                                   0x3FF, 0x0E0, 0x070, 0x038, 0x01C, 0x00E, 0x006};
constexpr int BOLT_LARGE_WIDTH = 10;

// Centres the bolt in a body of bodyWidth x bodyHeight at (x, y); the body stays empty around it.
void drawChargingBolt(const GfxRenderer& r, const int x, const int y, const int bodyWidth, const int bodyHeight,
                      const bool large) {
  const uint16_t* rows = large ? BOLT_LARGE : BOLT_SMALL;
  const int count = large ? static_cast<int>(std::size(BOLT_LARGE)) : static_cast<int>(std::size(BOLT_SMALL));
  const int width = large ? BOLT_LARGE_WIDTH : BOLT_SMALL_WIDTH;
  const int left = x + (bodyWidth - width) / 2;
  const int top = y + (bodyHeight - count) / 2;
  for (int j = 0; j < count; ++j) {
    for (int i = 0; i < width; ++i) {
      if (rows[j] >> i & 1u) r.drawPixel(left + i, top + j);
    }
  }
}

void drawSiblingChevron(const GfxRenderer& r, const int x, const int y, const bool pointsRight) {
  tenorchrome::drawMoreChevron(r, x, y, pointsRight ? tenorchrome::ChevronDir::Right : tenorchrome::ChevronDir::Left,
                               SIBLING_CHEVRON_SPAN);
}
}  // namespace

void tenorchrome::drawSiblingDestinations(const GfxRenderer& r, const char* previous, const char* next) {
  // Mot bac to hon SMALL: day la thu noi cho nguoi dung biet hai nut hai ben di
  // dau, nen no phai doc duoc luot qua.
  constexpr int font = UI_10_FONT_ID;
  constexpr int tracking = 1;
  const int routeLineHeight = r.getLineHeight(UI_12_FONT_ID);
  const int routeY = HEADER_TOP + (headerHeight() - routeLineHeight) / 2;
  const int siblingLineHeight = r.getLineHeight(font);
  // Neo vao GIUA dai the, khong phai treo duoi dong duong dan. Dai the bi an o
  // man nay, nhung no van la o ma hai ten nay thuoc ve, nen canh giua theo no
  // thi hai ten dung dung cho du man co ve dai hay khong.
  const int siblingY = tabTop() + (tabHeight() - siblingLineHeight) / 2;
  // Tran duoi la day dai the. Truoc day tran nay bi keo len vi goc phai con ve
  // day dong "1-10 / 13"; bo con so do roi thi ten the duoc dung het dai.
  if (routeLineHeight <= 0 || siblingLineHeight <= 0 || siblingY < routeY + routeLineHeight ||
      siblingY + siblingLineHeight > tabTop() + tabHeight())
    return;

  const int width = r.getScreenWidth();
  const int minimumWidth = 2 * SIBLING_EDGE + SIBLING_CENTER_GAP + 2 * SIBLING_CHEVRON_WIDTH;
  if (width < minimumWidth) return;
  const int slotWidth = (width - 2 * SIBLING_EDGE - SIBLING_CENTER_GAP) / 2;
  const int textMax = std::max(0, slotWidth - SIBLING_CHEVRON_WIDTH - SIBLING_LABEL_GAP);
  const std::string left =
      r.truncatedText(font, previous ? previous : "", textMax, EpdFontFamily::REGULAR, tracking);
  const std::string right = r.truncatedText(font, next ? next : "", textMax, EpdFontFamily::REGULAR, tracking);

  const int leftChevronX = SIBLING_EDGE;
  const int leftTextX = leftChevronX + SIBLING_CHEVRON_WIDTH + SIBLING_LABEL_GAP;
  const int rightChevronX = width - SIBLING_EDGE - SIBLING_CHEVRON_WIDTH;
  const int rightTextWidth = r.getTextWidth(font, right.c_str(), EpdFontFamily::REGULAR,
                                            BidiUtils::BidiBaseDir::AUTO, tracking);
  const int rightTextX = rightChevronX - SIBLING_LABEL_GAP - rightTextWidth;
  const int chevronY = siblingY + (siblingLineHeight - SIBLING_CHEVRON_HEIGHT) / 2;

  drawSiblingChevron(r, leftChevronX, chevronY, false);
  r.drawText(font, leftTextX, siblingY, left.c_str(), true, EpdFontFamily::REGULAR, BidiUtils::BidiBaseDir::AUTO,
             tracking);
  r.drawText(font, rightTextX, siblingY, right.c_str(), true, EpdFontFamily::REGULAR, BidiUtils::BidiBaseDir::AUTO,
             tracking);
  drawSiblingChevron(r, rightChevronX, chevronY, true);
}

tenorchrome::StatusCornerBounds tenorchrome::statusCornerBounds(const GfxRenderer& r, const bool large) {
  const int fontChu = large ? UI_12_FONT_ID : SMALL_FONT_ID;
  const int batteryWidth = large ? 32 : 26;
  char clock[12] = "--:--";
  halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1);
  const int timeWidth = r.getTextWidth(fontChu, clock);
  const int percent = std::max(0, std::min(100, static_cast<int>(powerManager.getDisplayedBatteryPercentage())));
  char percentage[8];
  snprintf(percentage, sizeof(percentage), "%d", percent);
  const int batteryBlock = batteryWidth + BATTERY_TEXT_GAP + r.getTextWidth(fontChu, percentage);
  const bool swap = SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
  if (swap)
    return {STATUS_CORNER_INSET + timeWidth, r.getScreenWidth() - STATUS_CORNER_INSET - batteryBlock};
  return {STATUS_CORNER_INSET + batteryBlock, r.getScreenWidth() - STATUS_CORNER_INSET - timeWidth};
}

// Mot lan chu dung chung cho moi menu va trinh doc. Mot lan ve chi doc dong ho khi
// duoc phep. Trong trinh doc, thanh nay theo dung sau muc nguoi dung chon
// (StatusBarSpec); ngoai trinh doc no theo ba muc cua thanh chung.
void tenorchrome::drawStatus(const GfxRenderer& r, const char* title, int currentPage, int pageCount,
                             float bookProgress, int paddingBottom, bool estimated, bool bookmarked,
                             bool titleIsName) {
  const bool trongTrinhDoc = title != nullptr;
  if (trongTrinhDoc ? SETTINGS.readerStatusBarHidden() : SETTINGS.globalStatusBarHidden()) return;
  const auto spec = SETTINGS.statusBarSpec();
  const bool hienPin = trongTrinhDoc ? spec.showBattery : true;
  const bool hienPhanTram = (trongTrinhDoc ? spec.showBatteryPercent : true) && SETTINGS.batteryPercentShown(trongTrinhDoc);
  const bool hienGio = trongTrinhDoc ? spec.showsClock() : true;
  const bool hienTieuDe = trongTrinhDoc && spec.showsTitle();
  const bool hienSoTrang = trongTrinhDoc && spec.showChapterPageCount;
  // A negative bookProgress is unknown (a book still building its index).
  const bool hienTienDo = trongTrinhDoc && spec.showBookProgressPercent && bookProgress >= 0;
  const int width = r.getScreenWidth();
  const bool lon = !trongTrinhDoc && SETTINGS.globalStatusBarLarge();
  const int y = statusTextY(r.getScreenHeight(), lon, paddingBottom);
  const int batteryWidth = lon ? 32 : 26;
  const int batteryHeight = lon ? 18 : 14;
  const int fontChu = lon ? UI_12_FONT_ID : SMALL_FONT_ID;
  const bool swap = SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
  char clock[12] = "--:--";
  halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1);
  const int timeWidth = r.getTextWidth(fontChu, clock);
  if (hienGio)
    r.drawText(fontChu, swap ? STATUS_CORNER_INSET : width - STATUS_CORNER_INSET - timeWidth, y, clock);
  const int by = statusIconTopY(r.getScreenHeight(), lon, paddingBottom);
  const int percent = std::max(0, std::min(100, static_cast<int>(powerManager.getDisplayedBatteryPercentage())));
  char percentage[8];
  snprintf(percentage, sizeof(percentage), "%d", percent);
  const int batteryTextWidth = hienPhanTram ? r.getTextWidth(fontChu, percentage) : 0;
  const int batteryBlock = batteryWidth + (hienPhanTram ? BATTERY_TEXT_GAP + batteryTextWidth : 0);
  // Pin nam o ben DOI dien voi dong ho (hoac ben trai khi khong hien dong ho), nen
  // muc 5 (ten chuong & pin) khong day pin sang phai nhu khi vang dong ho.
  const bool batteryRight = hienGio && swap;
  const int batteryBlockX = batteryRight ? width - STATUS_CORNER_INSET - batteryBlock : STATUS_CORNER_INSET;
  const int bx = batteryRight ? width - STATUS_CORNER_INSET - batteryWidth : batteryBlockX;
  if (hienPin) {
    // Tong be rong gom ca dau pin 2 px, de vien ngoai dung inset.
    const int bodyWidth = batteryWidth - 2;
    // Blocks are sampled at pixel centres, so a corner of 5 px draws the body's corners with the
    // very pixels the battery has always had (3, 1, 1 per row); the charge inside is set 2 px in,
    // concentric with it.
    constexpr int BATTERY_RADIUS = 5;
    r.drawRoundedRect(bx, by, bodyWidth, batteryHeight, 1, BATTERY_RADIUS, true);
    r.fillRect(bx + bodyWidth, by + (batteryHeight - 4) / 2, 2, 4);
    const int fill = ((bodyWidth - 4) * percent + 50) / 100;
    if (gpio.isUsbConnected()) {
      // Charging: the bolt alone, the level stays readable in the number beside it.
      drawChargingBolt(r, bx, by, bodyWidth, batteryHeight, lon);
    } else if (fill > 0) {
      r.fillRoundedRect(bx + 2, by + 2, fill, batteryHeight - 4, tenorradius::nest(BATTERY_RADIUS, 2), Color::Black);
    }
    if (hienPhanTram) {
      const int textX = batteryRight ? batteryBlockX : bx + batteryWidth + BATTERY_TEXT_GAP;
      r.drawText(fontChu, textX, y, percentage);
    }
  }
  if (!hienTieuDe && !hienSoTrang && !hienTienDo) return;
  // Hai ben neo vao dung khoi goc dang co, cach mot khoang nho.
  const int trai =
      (hienGio && swap ? STATUS_CORNER_INSET + timeWidth
                       : (hienPin && !batteryRight ? STATUS_CORNER_INSET + batteryBlock : STATUS_CORNER_INSET)) +
      14;
  const int phai = (hienGio && !swap ? width - STATUS_CORNER_INSET - timeWidth
                                     : (hienPin && batteryRight ? batteryBlockX : width - STATUS_CORNER_INSET)) -
                   14;
  std::string counts;
  if (hienSoTrang) {
    char phan[24];
    snprintf(phan, sizeof(phan), "%s%d/%d", estimated ? "~" : "", currentPage, pageCount);
    counts = phan;
  }
  if (hienTienDo) {
    char phan[16];
    // Two spaces: one space in the small font reads "1/1 100%" as "1/1100%".
    snprintf(phan, sizeof(phan), "%s%.0f%%", counts.empty() ? "" : "  ",
             std::max(0.0f, std::min(100.0f, bookProgress)));
    counts += phan;
  }
  const int countWidth = counts.empty() ? 0 : r.getTextWidth(SMALL_FONT_ID, counts.c_str());
  const int countX = phai - countWidth;
  if (!counts.empty()) r.drawText(SMALL_FONT_ID, countX, y, counts.c_str());
  const int markWidth = bookmarked ? 16 : 0;
  if (bookmarked) inlineSymbols::drawShape(r, inlineSymbols::Shape::Star, trai + 6, y + 12, 10, true);
  if (!hienTieuDe) return;
  const int room = std::max(0, countX - trai - markWidth - 8);
  // The colon only separates a name from counts; alone, or after a note, it would dangle.
  const int colon = counts.empty() || !titleIsName ? 0 : r.getTextWidth(SMALL_FONT_ID, ":");
  std::string name = r.truncatedText(SMALL_FONT_ID, title, std::max(0, room - colon));
  // A room narrower than the ellipsis still gets one back, so measure what came back
  // and give up the name rather than draw it into the battery or the clock.
  if (name.empty() || r.getTextWidth(SMALL_FONT_ID, name.c_str()) + colon > room) return;
  if (colon) name += ":";
  r.drawText(SMALL_FONT_ID, trai + markWidth, y, name.c_str());
}

// Arms rise 8 px in 11, the slope of the list's down chevron readers already know. The stroke is
// three pixels along the pointing axis, about 2.4 px across each arm: two (1.7 px) read faint
// beside bold text on the panel. Drawn one short run per step across the axis, from |t| alone, so
// left and right are exact mirror images.
void tenorchrome::drawMoreChevron(const GfxRenderer& r, const int x, const int y, const ChevronDir dir,
                                  const int span) {
  const int depth = moreChevronLength(span) - MORE_CHEVRON_STROKE;
  for (int t = -span; t <= span; ++t) {
    const int along = depth - ((t < 0 ? -t : t) * depth * 2 + span) / (2 * span);
    const int a = dir == ChevronDir::Left ? depth - along : along;
    if (dir == ChevronDir::Down)
      r.drawLine(x + span + t, y + a, x + span + t, y + a + MORE_CHEVRON_STROKE - 1);
    else
      r.drawLine(x + a, y + span + t, x + a + MORE_CHEVRON_STROKE - 1, y + span + t);
  }
}

int tenorchrome::moreBelowChevronTopY(const GfxRenderer& renderer, const int hintTopY) {
  const int top = tipY(renderer) - 30;
  // The chevron is moreChevronLength(MORE_BELOW_SPAN) rows tall. Leave two clear rows before the first tip.
  return hintTopY >= 0 ? std::min(top, hintTopY - moreChevronLength(MORE_BELOW_SPAN) - 2) : top;
}

void tenorchrome::drawMoreBelowChevron(const GfxRenderer& renderer, const int hintTopY) {
  drawMoreChevron(renderer, renderer.getScreenWidth() / 2 - MORE_BELOW_SPAN, moreBelowChevronTopY(renderer, hintTopY),
                  ChevronDir::Down, MORE_BELOW_SPAN);
}

int tenorchrome::smallFooterSymbolsTopY(const GfxRenderer& renderer) {
  return statusIconTopY(renderer.getScreenHeight(), false) - 1;
}

bool tenorchrome::compactFooterTips(const bool hasTextHints) {
  return enabled() && SETTINGS.tenorButtonSymbols && !SETTINGS.globalStatusBarHidden() &&
         !SETTINGS.globalStatusBarLarge() && !hasTextHints;
}

int tenorchrome::tipY(const GfxRenderer& renderer, const bool hasTextHints) {
  if (compactFooterTips(hasTextHints)) {
    return smallFooterSymbolsTopY(renderer) - 2 - renderer.getLineHeight(SMALL_FONT_ID);
  }
  return renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight - (renderer.getLineHeight(SMALL_FONT_ID) + 5);
}

namespace {
std::vector<std::string> tipLines(const GfxRenderer& renderer, const char* text, const int maxLines) {
  std::vector<std::string> lines;
  if (!text || maxLines <= 0) return lines;

  const std::string value(text);
  size_t start = 0;
  while (start <= value.size() && static_cast<int>(lines.size()) < maxLines) {
    const size_t end = value.find('\n', start);
    const std::string paragraph = value.substr(start, end == std::string::npos ? end : end - start);
    if (paragraph.empty()) {
      lines.emplace_back();
    } else {
      const auto wrapped = renderer.wrappedText(SMALL_FONT_ID, paragraph.c_str(), renderer.getScreenWidth() - 48,
                                                maxLines - static_cast<int>(lines.size()));
      lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return lines;
}

int tipLineY(const GfxRenderer& renderer, const std::vector<std::string>& lines, const int linesAbove,
             const bool hasTextHints) {
  constexpr int font = SMALL_FONT_ID;
  const int lineHeight = renderer.getLineHeight(font);
  int y = tenorchrome::tipY(renderer, hasTextHints) -
          (linesAbove + static_cast<int>(lines.size()) - 1) * lineHeight;
  if (!tenorchrome::compactFooterTips(hasTextHints)) return y;

  int inkBottom = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    const int bottom = std::max(renderer.getFontAscenderSize(font) - 1,
                                renderer.getTextInkBottom(font, lines[i].c_str(), EpdFontFamily::REGULAR));
    inkBottom = std::max(inkBottom, static_cast<int>(i) * lineHeight + bottom);
  }
  return y + static_cast<int>(lines.size()) * lineHeight - inkBottom;
}
}  // namespace

int tenorchrome::tipTopY(const GfxRenderer& renderer, const char* text, const int linesAbove, const int maxLines,
                         const bool hasTextHints) {
  constexpr int font = SMALL_FONT_ID;
  const auto lines = tipLines(renderer, text, maxLines);
  if (lines.empty()) return tipY(renderer, hasTextHints);

  const int lineHeight = renderer.getLineHeight(font);
  const int y = tipLineY(renderer, lines, linesAbove, hasTextHints);
  int top = y;
  bool hasInk = false;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (renderer.getTextInkBottom(font, lines[i].c_str(), EpdFontFamily::REGULAR) <= 0) continue;
    const int lineTop = y + static_cast<int>(i) * lineHeight +
                        renderer.getTextInkTop(font, lines[i].c_str(), EpdFontFamily::REGULAR);
    top = hasInk ? std::min(top, lineTop) : lineTop;
    hasInk = true;
  }
  return top;
}

int tenorchrome::tipLineCount(const GfxRenderer& renderer, const char* text, const int maxLines) {
  return static_cast<int>(tipLines(renderer, text, maxLines).size());
}

int tenorchrome::tipHeight(const GfxRenderer& renderer, const char* text, int maxLines) {
  constexpr int font = SMALL_FONT_ID;
  const auto lines = tipLines(renderer, text, maxLines);
  return lines.empty() ? 0 : renderer.getLineHeight(font) + 7 + (static_cast<int>(lines.size()) - 1) * renderer.getLineHeight(font);
}
void tenorchrome::drawTip(const GfxRenderer& renderer, const char* text, int linesAbove, int maxLines,
                          const bool hasTextHints) {
  // Global status-bar Off also hides contextual footer tips.
  if (SETTINGS.globalStatusBarHidden()) return;
  constexpr int font = SMALL_FONT_ID;
  const auto lines = tipLines(renderer, text, maxLines);
  if (lines.empty()) return;
  const int lineHeight = renderer.getLineHeight(font);
  int y = tipLineY(renderer, lines, linesAbove, hasTextHints);
  for (const auto& line : lines) {
    renderer.drawCenteredText(font, y, line.c_str());
    y += renderer.getLineHeight(font);
  }
}
