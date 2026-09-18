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

namespace {
constexpr int SIBLING_CHEVRON_WIDTH = 6;
constexpr int SIBLING_CHEVRON_HEIGHT = 8;
constexpr int SIBLING_EDGE = 18;
constexpr int SIBLING_LABEL_GAP = 6;
constexpr int SIBLING_CENTER_GAP = 16;

void drawSiblingChevron(const GfxRenderer& r, const int x, const int y, const bool pointsRight) {
  const int mid = y + SIBLING_CHEVRON_HEIGHT / 2;
  const int tip = x + SIBLING_CHEVRON_WIDTH - 1;
  if (pointsRight) {
    r.drawLine(x, y, tip, mid);
    r.drawLine(tip, mid, x, y + SIBLING_CHEVRON_HEIGHT - 1);
  } else {
    r.drawLine(tip, y, x, mid);
    r.drawLine(x, mid, tip, y + SIBLING_CHEVRON_HEIGHT - 1);
  }
}
}  // namespace

void tenorchrome::drawSiblingDestinations(const GfxRenderer& r, const char* previous, const char* next) {
  // Mot bac to hon SMALL: day la thu noi cho nguoi dung biet hai nut hai ben di
  // dau, nen no phai doc duoc luot qua.
  constexpr int font = UI_10_FONT_ID;
  constexpr int tracking = 1;
  const int routeLineHeight = r.getLineHeight(UI_12_FONT_ID);
  const int routeY = HEADER_TOP + (HEADER_HEIGHT - routeLineHeight) / 2;
  const int siblingLineHeight = r.getLineHeight(font);
  // Neo vao GIUA dai the, khong phai treo duoi dong duong dan. Dai the bi an o
  // man nay, nhung no van la o ma hai ten nay thuoc ve, nen canh giua theo no
  // thi hai ten dung dung cho du man co ve dai hay khong.
  const int siblingY = TAB_TOP + (TAB_HEIGHT - siblingLineHeight) / 2;
  // Tran duoi la day dai the. Truoc day tran nay bi keo len vi goc phai con ve
  // day dong "1-10 / 13"; bo con so do roi thi ten the duoc dung het dai.
  if (routeLineHeight <= 0 || siblingLineHeight <= 0 || siblingY < routeY + routeLineHeight ||
      siblingY + siblingLineHeight > TAB_TOP + TAB_HEIGHT)
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
  constexpr int inset = 12;
  const int fontChu = large ? UI_12_FONT_ID : SMALL_FONT_ID;
  const int batteryWidth = large ? 32 : 26;
  char clock[12] = "--:--";
  halClock.formatTime(clock, sizeof(clock), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1);
  const int timeWidth = r.getTextWidth(fontChu, clock);
  const int percent = std::max(0, std::min(100, static_cast<int>(powerManager.getBatteryPercentage())));
  char percentage[8];
  snprintf(percentage, sizeof(percentage), "%d", percent);
  const int batteryBlock = batteryWidth + 7 + r.getTextWidth(fontChu, percentage);
  const bool swap = SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
  if (swap) return {inset + timeWidth, r.getScreenWidth() - inset - batteryBlock};
  return {inset + batteryBlock, r.getScreenWidth() - inset - timeWidth};
}

// Mot lan chu dung chung cho moi menu va trinh doc. Mot lan ve chi doc dong ho khi
// duoc phep. Trong trinh doc, thanh nay theo dung sau muc nguoi dung chon
// (StatusBarSpec); ngoai trinh doc no theo ba muc cua thanh chung.
void tenorchrome::drawStatus(const GfxRenderer& r, const char* title, int currentPage, int pageCount,
                             float bookProgress, int paddingBottom, bool estimated, bool bookmarked) {
  const bool trongTrinhDoc = title != nullptr;
  if (trongTrinhDoc ? SETTINGS.readerStatusBarHidden() : SETTINGS.globalStatusBarHidden()) return;
  const auto spec = SETTINGS.statusBarSpec();
  const bool hienPin = trongTrinhDoc ? spec.showBattery : true;
  const bool hienPhanTram = trongTrinhDoc ? spec.showBatteryPercent : true;
  const bool hienGio = trongTrinhDoc ? spec.showsClock() : true;
  const bool hienTieuDe = trongTrinhDoc && spec.showsTitle();
  const bool hienSoTrang = trongTrinhDoc && spec.showChapterPageCount;
  const bool hienTienDo = trongTrinhDoc && spec.showBookProgressPercent;
  const int width = r.getScreenWidth();
  const bool lon = !trongTrinhDoc && SETTINGS.globalStatusBarLarge();
  const int y = statusTextY(r.getScreenHeight(), lon, paddingBottom);
  constexpr int inset = 12;
  const int batteryWidth = lon ? 32 : 26;
  const int batteryHeight = lon ? 18 : 14;
  const int fontChu = lon ? UI_12_FONT_ID : SMALL_FONT_ID;
  const bool swap = SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
  char clock[12] = "--:--";
  halClock.formatTime(clock, sizeof(clock), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1);
  const int timeWidth = r.getTextWidth(fontChu, clock);
  if (hienGio) r.drawText(fontChu, swap ? inset : width - inset - timeWidth, y, clock);
  const int by = statusIconTopY(r.getScreenHeight(), lon, paddingBottom);
  const int percent = std::max(0, std::min(100, static_cast<int>(powerManager.getBatteryPercentage())));
  char percentage[8];
  snprintf(percentage, sizeof(percentage), "%d", percent);
  const int batteryTextWidth = hienPhanTram ? r.getTextWidth(fontChu, percentage) : 0;
  const int batteryBlock = batteryWidth + (hienPhanTram ? 7 + batteryTextWidth : 0);
  // Pin nam o ben DOI dien voi dong ho (hoac ben trai khi khong hien dong ho), nen
  // muc 5 (ten chuong & pin) khong day pin sang phai nhu khi vang dong ho.
  const int bx = (hienGio && swap) ? width - inset - batteryBlock : inset;
  if (hienPin) {
    // Khoi pin lien mach, vien bo goc, va vien van thay khi pin can.
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
    if (hienPhanTram) r.drawText(fontChu, bx + batteryWidth + 7, y, percentage);
  }
  if (!hienTieuDe && !hienSoTrang && !hienTienDo) return;
  // Hai ben neo vao dung khoi goc dang co, cach mot khoang nho.
  const int trai = (hienGio && swap ? inset + timeWidth : (hienPin && bx == inset ? inset + batteryBlock : inset)) + 14;
  const int phai = (hienGio && !swap ? width - inset - timeWidth : (hienPin && bx != inset ? bx : width - inset)) - 14;
  std::string counts;
  if (hienSoTrang) {
    char phan[24];
    snprintf(phan, sizeof(phan), "%s%d/%d", estimated ? "~" : "", currentPage, pageCount);
    counts = phan;
  }
  if (hienTienDo) {
    char phan[16];
    snprintf(phan, sizeof(phan), "%s%.0f%%", counts.empty() ? "" : " ",
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
  std::string name = r.truncatedText(SMALL_FONT_ID, title, std::max(0, room - r.getTextWidth(SMALL_FONT_ID, ":")));
  if (!name.empty()) name += ":";
  if (!name.empty()) r.drawText(SMALL_FONT_ID, trai + markWidth, y, name.c_str());
}

void tenorchrome::drawMoreBelowChevron(const GfxRenderer& renderer) {
  constexpr int HALF_WIDTH = 11, HEIGHT = 7, THICKNESS = 2;
  const int cx = renderer.getScreenWidth() / 2;
  const int top = tipY(renderer) - 30;
  // Hai nhip day mot diem: net mot diem tren e-ink nhat qua, nhin khong ra hinh.
  for (int d = 0; d < THICKNESS; ++d) {
    renderer.drawLine(cx - HALF_WIDTH, top + d, cx, top + HEIGHT + d);
    renderer.drawLine(cx, top + HEIGHT + d, cx + HALF_WIDTH, top + d);
  }
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
  // Global status-bar Off also hides contextual footer tips.
  if (SETTINGS.globalStatusBarHidden()) return;
  constexpr int font = SMALL_FONT_ID;
  const auto lines = renderer.wrappedText(font, text, renderer.getScreenWidth() - 48, maxLines);
  int y = tipY(renderer) - (linesAbove + static_cast<int>(lines.size()) - 1) * renderer.getLineHeight(font);
  for (const auto& line : lines) {
    renderer.drawCenteredText(font, y, line.c_str());
    y += renderer.getLineHeight(font);
  }
}
