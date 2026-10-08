#include "TenorMenuChrome.h"
#include "activities/reader/ReaderStatusLayout.h"

#include <GfxRenderer.h>
#include <InlineSymbols.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <BlePageTurner.h>
#include <HalPowerManager.h>
#include <WiFi.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <cstdio>
#include <string>
#include <vector>

#include "ButtonSymbols.h"
#include "ClockStatus.h"
#include "HeaderBackTapTarget.h"
#include "activities/Activity.h"
#include "activities/reader/ReaderMenuLayout.h"
#include "StatusGlyphs.h"
#include "icons/tenorHomeTabIcons.h"
#include "icons/tenorStatusIcons.h"
#include "icons/tenorReaderTabIcons.h"
#include "UITheme.h"
#include "fontIds.h"
#include "themes/TenorRadius.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
#include "Epub/converters/DirectPixelWriter.h"
#endif

namespace {
// A round-ended bar (radius h / 2) in pixel-centre arithmetic doubled to stay integer.
bool inPill(const int px, const int py, const int x, const int y, const int w, const int h) {
  if (px < x || py < y || px >= x + w || py >= y + h) return false;
  const int r = h / 2;
  int dx = 0;
  if (px < x + r) {
    dx = 2 * (x + r) - (2 * px + 1);
  } else if (px >= x + w - r) {
    dx = (2 * px + 1) - 2 * (x + w - r);
  } else {
    return true;
  }
  const int dy = 2 * py + 1 - (2 * y + h);
  return dx * dx + dy * dy < 4 * r * r;
}

// A box with corners of radius r, same doubled arithmetic.
bool inRound(const int px, const int py, const int x, const int y, const int w, const int h, const int r) {
  if (px < x || py < y || px >= x + w || py >= y + h) return false;
  int dx = 0, dy = 0;
  if (px < x + r) dx = 2 * (x + r) - (2 * px + 1);
  else if (px >= x + w - r) dx = (2 * px + 1) - 2 * (x + w - r);
  if (py < y + r) dy = 2 * (y + r) - (2 * py + 1);
  else if (py >= y + h - r) dy = (2 * py + 1) - 2 * (y + h - r);
  return dx <= 0 || dy <= 0 || dx * dx + dy * dy < 4 * r * r;
}

}  // namespace

void tenorchrome::drawRoundRing(const GfxRenderer& g, const int x, const int y, const int w, const int h, const int r,
                                const int thick, const bool grey) {
  const auto plot = [&](const int px, const int py) {
    if (!grey || ((px + py) & 1) == 0) g.drawPixel(px, py, true);
  };
  const int ri = std::max(0, r - thick);
  // A corner row is tested only across its 2 corners; between them it is ink in the top and bottom `thick` rows
  // and empty elsewhere (the same pixels as testing every one, which took 2/3 of a 448 px popup frame).
  const bool corners = r >= thick && w >= 2 * r;
  const auto ring = [&](const int px, const int py) {
    if (inRound(px, py, x, y, w, h, r) && !inRound(px, py, x + thick, y + thick, w - 2 * thick, h - 2 * thick, ri))
      plot(px, py);
  };
  for (int py = y; py < y + h; ++py) {
    if (py >= y + r && py < y + h - r) {
      // Straight sides: only the two edges.
      for (int i = 0; i < thick; ++i) {
        plot(x + i, py);
        plot(x + w - 1 - i, py);
      }
      continue;
    }
    if (!corners) {
      for (int px = x; px < x + w; ++px) ring(px, py);
      continue;
    }
    for (int px = x; px < x + r; ++px) ring(px, py);
    if (py < y + thick || py >= y + h - thick)
      for (int px = x + r; px < x + w - r; ++px) plot(px, py);
    for (int px = x + w - r; px < x + w; ++px) ring(px, py);
  }
}

// Ring of `thick` px just inside a round-ended bar; grey = every other pixel, (x + y) even.
void tenorchrome::drawPillRing(const GfxRenderer& g, const int x, const int y, const int w, const int h, const int thick,
                  const bool grey) {
  const int r = h / 2;
  const auto plot = [&](const int px, const int py) {
    if (!grey || ((px + py) & 1) == 0) g.drawPixel(px, py, true);
  };
  for (int py = y; py < y + h; ++py) {
    const bool band = py < y + thick || py >= y + h - thick;
    for (int px = x; px < x + w; ++px) {
      if (px >= x + r && px < x + w - r) {
        if (band) plot(px, py);
        continue;
      }
      if (inPill(px, py, x, y, w, h) && !inPill(px, py, x + thick, y + thick, w - 2 * thick, h - 2 * thick))
        plot(px, py);
    }
  }
}


namespace {
// Screens below another one, with a back to it: the settings groups and what they open, the file and
// library lists, Wi-Fi, the reader's contents. Dialogs, the reader page and the like are not here.
constexpr const char* FULL_BAR[] = {
    "About", "BlePageTurner", "BookStats", "BookStatsLibrary", "ButtonRemap", "CalibreConnect", "ClearCache",
    "ClockSettings", "ClockSync", "CrossPointWebServer", "DictionaryDefinition", "EpubReaderBookmarks",
    "EpubReaderChapterSelection", "EpubReaderFootnoteSelect", "FileBrowser", "FontDownload", "HomeButtonSettings",
    "InfoUpdate", "KOReaderAuth", "KOReaderSettings", "KOReaderSync", "KeyboardLayouts", "LanguageSelect",
    "NetworkModeSelection", "OpdsBookBrowser", "OpdsServerList", "OpdsSettings", "QrDisplay",
    "QuoteDetail", "QuoteTrim", "Quotes", "ReadingHabits", "ReadingHistory", "Settings",
    "StatusBarSettings", "TimezonePicker", "WifiSelection", "XtcReaderChapterSelection"};
// "<" at the left, the screen's own content in the rest of the foot: the keyboard of an input screen,
// the cards of a screen with cards (the reader menu: "<" goes back a level, from the top level it
// closes the menu; the text settings; the library), a screen laid over another one (a question, a slider,
// a word picked on the page, the light panel, a picture), where "<" cancels, and a screen that must leave
// through its own exit (a firmware update, the card lent to a computer): no zone icon to jump away by.
constexpr const char* BACK_ONLY_BAR[] = {"KeyboardEntry", "EpubReaderMenu", "TextSettings", "Library",
                                         "BmpViewer", "ChapterNumberEntry", "Confirmation", "Crash",
                                         "DictionaryWordSelect", "EpubReaderPercentSelection", "FrontlightPanel",
                                         "OtaUpdate", "QuoteSelect", "SdFirmwareUpdate", "SleepTimeoutInterval",
                                         "UglySwitch", "UsbDrive"};
// Zone roots that draw their own cards at the foot.
constexpr const char* TABS_BAR[] = {"Home"};

template <size_t N>
bool named(const char* const (&names)[N], const char* activityName) {
  for (const char* name : names)
    if (std::strcmp(name, activityName) == 0) return true;
  return false;
}

// The header's title, for the bar of the screen that drew it: a screen that draws no header gets no
// name from the one before it.
char noted[96] = {};
uint32_t notedGeneration = UINT32_MAX;
uint32_t handDrawnGeneration = UINT32_MAX;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
struct ReaderFootBarNote {
  bool open = false, keypad = false;
  int activeTool = -1;
  uint32_t generation = UINT32_MAX;
} readerFootBar;
bool readerFootBarActive() {
  const char* name = activityManager.currentName();
  return readerFootBar.open && readerFootBar.generation == activityManager.activityGeneration() &&
         name && strcmp(name, "EpubReader") == 0;
}
#endif

}  // namespace

// The one way an icon bar draws its icons (dynamic bar rule 2): the chosen one solid, the others grey, every
// other ink pixel of their Mask1 art (bit 0 = ink).
void tenorchrome::drawBarIcon(const GfxRenderer& r, const uint8_t* bits, const int w, const int h, const int x,
                              const int y, const bool chosen) {
  const int stride = (w + 7) / 8;
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i)
      if (((bits[j * stride + i / 8] >> (7 - i % 8)) & 1) == 0 && (chosen || ((i + j) & 1) == 0))
        r.drawPixel(x + i, y + j, true);
}

void tenorchrome::drawFavoriteMark(const GfxRenderer& r, const int x, const int fontId, const int lineTop) {
  // The Favourites tab's heart, filled and shrunk, worked out once: inside the outline is everything the
  // outside, flooded from the icon's border, does not reach; a mark pixel is ink when most of its block is.
  static uint32_t rows[FAVORITE_MARK];
  static bool ready = false;
  if (!ready) {
    const auto& icon = icon_tenor_home_favorites_bold_40;
    constexpr int S = 40;
    static_assert(FAVORITE_MARK <= 32, "a mark row fits 32 bits");
    const int stride = (icon.w + 7) / 8;
    bool outside[S][S] = {};
    const auto ink = [&](const int u, const int v) {
      return u < icon.w && v < icon.h && ((icon.bits[v * stride + u / 8] >> (7 - u % 8)) & 1) == 0;
    };
    for (bool grew = true; grew;) {
      grew = false;
      for (int v = 0; v < S; ++v)
        for (int u = 0; u < S; ++u) {
          if (outside[v][u] || ink(u, v)) continue;
          if (u == 0 || v == 0 || u == S - 1 || v == S - 1 || outside[v][u - 1] || outside[v - 1][u] ||
              outside[v][u + 1] || outside[v + 1][u])
            outside[v][u] = grew = true;
        }
    }
    constexpr int n = FAVORITE_MARK;
    for (int j = 0; j < n; ++j)
      for (int i = 0; i < n; ++i) {
        int filled = 0, all = 0;
        for (int v = j * S / n; v < (j + 1) * S / n; ++v)
          for (int u = i * S / n; u < (i + 1) * S / n; ++u, ++all) filled += !outside[v][u];
        if (2 * filled > all) rows[j] |= 1u << i;
      }
    ready = true;
  }
  const int y = inlineSymbols::markTopOnCapitals(r, fontId, lineTop, FAVORITE_MARK);
  for (int j = 0; j < FAVORITE_MARK; ++j)
    for (int i = 0; i < FAVORITE_MARK; ++i)
      if (rows[j] >> i & 1) r.drawPixel(x + i, y + j, true);
}

void tenorchrome::drawPanel(const GfxRenderer& g, const int y, const int h) {
  drawRoundRing(g, FOOT_BACK_X, y, g.getScreenWidth() - 2 * FOOT_BACK_X, h, PANEL_RADIUS, 2, true);
}

void tenorchrome::drawRowRule(const GfxRenderer& g, const int y, const int x0, const int x1) {
  for (int x = x0; x < x1; ++x)
    if (((x + y) & 1) == 0) g.drawPixel(x, y, true);
}

void tenorchrome::drawBarTab(const GfxRenderer& r, const int cx, const int y, const int h, const uint8_t* bits,
                             const int w, const int iconH, const bool chosen) {
  if (chosen) drawPillRing(r, cx - BAR_TAB_W / 2, y + BAR_TAB_INSET, BAR_TAB_W, h - 2 * BAR_TAB_INSET, 3, false);
  drawBarIcon(r, bits, w, iconH, cx - w / 2, y + (h - iconH) / 2, chosen);
}

namespace {
// Mask1 icon (bit 0 = ink), solid.
void drawIcon(const GfxRenderer& r, const freeink::Icon& icon, const int x, const int y) {
  tenorchrome::drawBarIcon(r, icon.bits, icon.w, icon.h, x, y, true);
}

const freeink::Icon& zoneIcon(const tenorchrome::Zone zone) {
  switch (zone) {
    case tenorchrome::Zone::File:
      return icon_tenor_home_folder_40;
    case tenorchrome::Zone::Favorites:
      return icon_tenor_home_favorites_40;
    case tenorchrome::Zone::Stats:
      return icon_tenor_home_stats_40;
    case tenorchrome::Zone::Settings:
      return icon_tenor_home_settings_40;
    case tenorchrome::Zone::Book:
      return icon_tenor_reader_reading_40;
    case tenorchrome::Zone::Recent:
      break;
  }
  return icon_tenor_home_recent_40;
}
}  // namespace

tenorchrome::FootBar tenorchrome::footBarFor(const char* activityName) {
  if (!kTouchShell || !activityName) return FootBar::None;
  if (named(FULL_BAR, activityName)) return FootBar::Full;
  if (named(BACK_ONLY_BAR, activityName)) return FootBar::BackOnly;
  if (named(TABS_BAR, activityName)) return FootBar::Tabs;
  return FootBar::None;
}

void tenorchrome::noteScreenTitle(const char* title) {
  snprintf(noted, sizeof(noted), "%s", title ? title : "");
  notedGeneration = activityManager.activityGeneration();
}

void tenorchrome::noteHandDrawn() { handDrawnGeneration = activityManager.activityGeneration(); }

const char* tenorchrome::screenTitle() {
  return notedGeneration == activityManager.activityGeneration() ? noted : "";
}

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
int tenorchrome::readerStripBottom(const GfxRenderer& r) {
  DirectPixelWriter pen;
  pen.init(const_cast<GfxRenderer&>(r));  // read only
  const int rowBits = pen.displayWidthBytes * 8, width = r.getScreenWidth();
  const auto ink = [&](const int x, const int y) {
    const int bit = (pen.phyYBase + x * pen.phyYStepX + y * pen.phyYStepY) * rowBits + pen.phyXBase + x * pen.phyXStepX +
                    y * pen.phyXStepY;
    return (pen.fb[bit >> 3] & (0x80 >> (bit & 7))) == 0;
  };
  // ponytail: at most 64 rows under the strip; a page with no blank row there keeps the strip's own band.
  const int top = contentTop();
  for (int y = top; y < top + 64; ++y) {
    bool any = false;
    for (int x = 0; x < width && !any; ++x) any = ink(x, y);
    if (!any) return y;
  }
  return top;
}

void tenorchrome::noteReaderFootBar(const bool open, const bool keypad, const int activeTool) {
  readerFootBar = {open, keypad, activeTool, activityManager.activityGeneration()};
  if (!open) HeaderBackTapTarget::clearFoot();
}
#endif

void tenorchrome::clearFootBand(const GfxRenderer& r) {
  const int top = footBackTop(r.getScreenHeight()) - 12;
  r.fillRect(0, top, r.getScreenWidth(), r.getScreenHeight() - top, false);
}

void tenorchrome::drawFootBar(const GfxRenderer& r, FootBar bar, const Zone zone) {
  HeaderBackTapTarget::clearFoot();
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  const bool reader = readerFootBarActive();
  if (reader) {
    bar = FootBar::BackOnly;
    // The reader owns this band while its menu is open, including the page's status footer.
    clearFootBand(r);
  }
#endif
  if (bar != FootBar::Full && bar != FootBar::BackOnly) return;
  constexpr int SIZE = FOOT_BACK_SIZE, ICON = 40;
  const int y = footBackTop(r.getScreenHeight());
  int x = FOOT_BACK_X;
  // On tenor/ugly paper (and in its reader menu) the "<" is drawn in pen: a hand circle round a hand chevron.
  bool pen = handDrawnGeneration == activityManager.activityGeneration();
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  pen = pen || (reader && shell::isUgly());
#endif
  if (pen) {
    ugly::circle(r, ugly::Circle::Object, {x + 8, y + 8, x + SIZE - 8, y + SIZE - 8}, 0, 0, 2);
    const int cx = x + SIZE / 2 - 2, cy = y + SIZE / 2;
    ugly::line(r, cx + 6, cy - 11, cx - 5, cy, 811, 3);
    ugly::line(r, cx - 5, cy, cx + 6, cy + 11, 812, 3);
  } else {
    // "<": the open chevron of the list marks, 3 px stroke, centred in its ring.
    drawPillRing(r, x, y, SIZE, SIZE, 2, true);
    constexpr int SPAN = 9;
    drawMoreChevron(r, x + (SIZE - moreChevronLength(SPAN)) / 2 - 1, y + SIZE / 2 - SPAN, ChevronDir::Left, SPAN);
  }
  HeaderBackTapTarget::setFoot(x, y, SIZE, SIZE);
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (reader && !readerFootBar.keypad) {
    using readermenu::Tool;
    const auto iconOf = [](const Tool tool, const bool bold) -> const freeink::Icon& {
      switch (tool) {
        case Tool::FAVORITES: return bold ? icon_tenor_home_favorites_bold_40 : icon_tenor_home_favorites_40;
        case Tool::CONTENTS: return bold ? icon_tenor_reader_position_bold_40 : icon_tenor_reader_position_40;
        case Tool::TEXT: return bold ? icon_tenor_reader_reading_bold_40 : icon_tenor_reader_reading_40;
        case Tool::MORE: break;
      }
      return bold ? icon_tenor_reader_tools_bold_40 : icon_tenor_reader_tools_40;
    };
    if (!shell::isUgly())
      drawPillRing(r, READER_BAR_LEFT, y, r.getScreenWidth() - FOOT_BACK_X - READER_BAR_LEFT, SIZE, 2, true);
    for (int i = 0; i < READER_TOOLS; ++i) {
      const auto cell = readerToolRect(r.getScreenWidth(), r.getScreenHeight(), i);
      const bool active = i == readerFootBar.activeTool;
      const auto tool = static_cast<Tool>(i);
      if (shell::isUgly()) {
        const auto name = ugly::fit(r, ugly::Size::S22, I18N.get(readermenu::toolName(tool)), cell.width - 16);
        const int textWidth = ugly::width(r, ugly::Size::S22, name.c_str());
        ugly::text(r, ugly::Size::S22, cell.x + (cell.width - textWidth) / 2,
                   y + (SIZE + ugly::ascent(ugly::Size::S22)) / 2, name.c_str());
        if (active) ugly::circle(r, ugly::Circle::Row,
            {cell.x + 8, y + 8, cell.x + cell.width - 8, y + SIZE - 8}, 0, 0, 2);
        continue;
      }
      const freeink::Icon& icon = iconOf(tool, active);
      drawBarTab(r, cell.x + cell.width / 2, y, SIZE, icon.bits, icon.w, icon.h, active);
    }
  }
#endif
  // Paper with a foot of its own keeps that foot: no zone icon, no name.
  if (bar == FootBar::BackOnly || pen) return;
  // The zone's icon, alone in its ring: a tap leads to the zone's root.
  x += SIZE + FOOT_PILL_GAP;
  drawPillRing(r, x, y, SIZE, SIZE, 2, true);
  drawIcon(r, zoneIcon(zone), x + (SIZE - ICON) / 2, y + (SIZE - ICON) / 2);
  HeaderBackTapTarget::setZone(x, y, SIZE, SIZE);
  // The screen's name in a pill that hugs it, cut with an ellipsis at the bar's end. Read only.
  const char* title = screenTitle();
  if (!*title) return;
  constexpr int PAD_LEFT = 18, PAD_RIGHT = 22, font = UI_12_FONT_ID;
  x += SIZE + FOOT_PILL_GAP;
  const int room = r.getScreenWidth() - FOOT_BACK_X - x - PAD_LEFT - PAD_RIGHT;
  const std::string name = r.truncatedText(font, title, room, EpdFontFamily::REGULAR);
  const int width = PAD_LEFT + r.getTextWidth(font, name.c_str(), EpdFontFamily::REGULAR) + PAD_RIGHT;
  drawPillRing(r, x, y, width, SIZE, 2, true);
  r.drawText(font, x + PAD_LEFT, y + (SIZE - r.getLineHeight(font)) / 2, name.c_str(), true, EpdFontFamily::REGULAR);
}

namespace {
// tenor/ugly: the names a screen came from, written before its title, cut by whole names and never in a name:
// all of them when they fit `room`, else the first and an ellipsis for the rest, else the ellipsis alone.
std::string uglyAncestors(const GfxRenderer& r, const char* prefix, const int room) {
  const std::string all = std::string(prefix) + "/";
  const auto fits = [&](const std::string& s) { return ugly::width(r, ugly::Size::S22, s.c_str()) <= room; };
  if (fits(all)) return all;
  const size_t slash = all.find('/');
  const std::string first = all.substr(0, slash) + "/\xE2\x80\xA6/";
  if (slash + 1 < all.size() && fits(first)) return first;
  return fits("\xE2\x80\xA6/") ? "\xE2\x80\xA6/" : "";
}
}  // namespace

void tenorchrome::drawHeader(const GfxRenderer& r, const char* title, const char* prefix, const char* note) {
  // Touch: no title row. The name goes to the bar at the foot (or the strip, on a screen without one);
  // the strip opens the top menu and going back is the bar's "<" (founder 05/10).
  if (kTouchShell) {
    (void)prefix;
    noteScreenTitle(title);
    HeaderBackTapTarget::clear();
    return;
  }
  if (shell::uglyParts()) {
    // The name written across the top and underlined; where it came from goes before it, smaller.
    const int base = HEADER_TOP + headerHeight() - 10;
    int x = 24, right = r.getScreenWidth() - 24;
    if (note && *note) {  // a count or a state, small, at the right end
      const std::string said = ugly::fit(r, ugly::Size::S22, note, (right - x) / 2);
      right -= ugly::width(r, ugly::Size::S22, said.c_str());
      ugly::text(r, ugly::Size::S22, right, base, said.c_str());
      right -= 12;
    }
    std::string from;
    if (prefix && *prefix) {
      from = uglyAncestors(r, prefix, (right - x) / 3);
      if (!from.empty()) x += ugly::text(r, ugly::Size::S22, x, base, from.c_str()) + 4;
    }
    const std::string name = ugly::fit(r, ugly::Size::S30, title ? title : "", right - x);
    ugly::text(r, ugly::Size::S30, x, base, name.c_str());
    ugly::underline(r, 20, r.getScreenWidth() - 20, base + 8, 760, 2);
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "part=header prefix=%s title=%s", from.c_str(), name.c_str());
#endif
    return;
  }
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

namespace {
// Touch status strip (dynamic bar rule 10): the clock at the left, a note in the middle, the radios
// that are on (18 px icons) and the battery at the right.
constexpr int STRIP_LEFT = 18, STRIP_ICON = 18, STRIP_ICON_GAP = 6, STRIP_BATTERY_GAP = 8, STRIP_NOTE_AIR = 12;
char stripNote[48] = {};
// "B" while the remote links: shown every other blink, so each blink is one refresh, and it stops at
// BT_LINK_GIVE_UP_MS (the strip calls it lost then).
constexpr unsigned long BT_BLINK_MS = 2000;
bool blinkShown = true;
uint32_t stripNoteGeneration = UINT32_MAX;

statusglyph::Bt bluetoothNow() {
  // Remembered across frames: a link seen once makes a later loss a loss, not a new attempt.
  static bool wasLinked = false;
  static unsigned long linkingSince = 0;
  const auto st = bleturner::status();
  const bool on = st.running || st.starting;
  if (st.connected) wasLinked = true;
  if (!on) wasLinked = false;
  if (!on || st.connected) linkingSince = 0;
  else if (!linkingSince) linkingSince = millis() | 1;
  return statusglyph::bluetooth(on, st.connected, wasLinked, linkingSince ? millis() - linkingSince : 0);
}

void drawStripMiddle(const GfxRenderer& r, const int y, const int clockEnd, const int batteryX, const int font) {
  int x = batteryX - STRIP_BATTERY_GAP;
  const int iconY = tenorchrome::TOUCH_STATUS_TOP_INSET +
                    (tenorchrome::TOUCH_STRIP_HEIGHT - tenorchrome::TOUCH_STATUS_TOP_INSET - STRIP_ICON) / 2;
  const auto icon = [&](const freeink::Icon& i) {
    x -= STRIP_ICON;
    drawIcon(r, i, x, iconY);
    x -= STRIP_ICON_GAP;
  };
  if (WiFi.getMode() != WIFI_MODE_NULL) icon(icon_status_wifi_18);
  switch (bluetoothNow()) {
    case statusglyph::Bt::Linked:
      icon(icon_status_bluetooth_18);
      break;
    case statusglyph::Bt::Lost:
      icon(icon_status_bluetooth_lost_18);
      break;
    case statusglyph::Bt::Linking: {
      const int w = r.getTextWidth(font, "B", EpdFontFamily::BOLD);
      x -= w;
      if (blinkShown) r.drawText(font, x, y, "B", true, EpdFontFamily::BOLD);
      x -= STRIP_ICON_GAP;
      break;
    }
    case statusglyph::Bt::None:
      break;
  }
  // The middle: what the screen notes (the items of a folder), else the name of a screen whose bar
  // does not hold it.
  const char* note = stripNoteGeneration == activityManager.activityGeneration() ? stripNote : "";
  if (!*note) {
    const auto bar = tenorchrome::footBarFor(activityManager.currentName());
    if (bar == tenorchrome::FootBar::None || bar == tenorchrome::FootBar::BackOnly) note = tenorchrome::screenTitle();
  }
  if (!*note) return;
  const int left = clockEnd + STRIP_NOTE_AIR, right = x + STRIP_ICON_GAP - STRIP_NOTE_AIR;
  const int half = std::min(r.getScreenWidth() / 2 - left, right - r.getScreenWidth() / 2);
  if (half <= 0) return;
  const std::string text = r.truncatedText(font, note, 2 * half);
  r.drawText(font, r.getScreenWidth() / 2 - r.getTextWidth(font, text.c_str()) / 2, y, text.c_str());
}
}  // namespace

bool tenorchrome::bluetoothBlinkDue() {
  static unsigned long last = 0;
  if (bluetoothNow() != statusglyph::Bt::Linking) {
    blinkShown = true;
    return false;
  }
  const unsigned long now = millis();
  if (now - last < BT_BLINK_MS) return false;
  last = now;
  blinkShown = !blinkShown;
  return true;
}

void tenorchrome::noteStatus(const char* text) {
  snprintf(stripNote, sizeof(stripNote), "%s", text ? text : "");
  stripNoteGeneration = activityManager.activityGeneration();
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
void tenorchrome::drawReaderSlots(const GfxRenderer& r, const char* title, const int currentPage,
                                 const int pageCount, const float bookProgress, const bool estimated,
                                 const bool bookmarked, const int64_t chapterSeconds, const int64_t bookSeconds) {
  using S = CrossPointSettings;
  const auto spec = SETTINGS.statusBarSpec();
  if (SETTINGS.readerStatusBarHidden() || !spec.slotsEnabled) return;
  const bool rough = shell::uglyParts();
  const int font = SMALL_FONT_ID;
  const auto text = [&](int x, int y, const char* value) {
    if (rough) ugly::text(r, ugly::Size::S22, x, y + 18, value);
    else r.drawText(font, x, y, value);
  };
  const auto width = [&](const char* value) {
    return rough ? ugly::width(r, ugly::Size::S22, value) : r.getTextWidth(font, value);
  };
  const auto fit = [&](const char* value, int room) {
    return rough ? ugly::fit(r, ugly::Size::S22, value, room) : r.truncatedText(font, value, room);
  };
  if (spec.topTitleMode != S::HIDE_TITLE) {
    const int mark = bookmarked ? 18 : 0;
    const int room = std::max(0, r.getScreenWidth() - 24 - mark);
    const auto name = fit(title ? title : "", room);
    if (width(name.c_str()) <= room) text(12 + mark, 8, name.c_str());
    if (bookmarked)
      inlineSymbols::drawShape(r, inlineSymbols::Shape::Star,
                               18, inlineSymbols::markTopOnCapitals(r, font, 8, 10) + 5, 10, true);
  }
  const int y = statusTextY(r.getScreenHeight(), false);
  const int iconTop = inlineSymbols::markTopOnCapitals(r, font, y, 14);
  const int markTop = inlineSymbols::markTopOnCapitals(r, font, y, 10);
  if (spec.topTitleMode == S::HIDE_TITLE && bookmarked && spec.textLaneVisible(true))
    inlineSymbols::drawShape(r, inlineSymbols::Shape::Star, 6, markTop + 5, 10, true);
  for (int slot = 0; slot < 3; ++slot) {
    const auto lane = readerstatus::cell(r.getScreenWidth(), slot);
    char value[64] = "";
    switch (spec.slots[slot]) {
      case S::STATUS_SLOT_NONE: continue;
      case S::STATUS_SLOT_CLOCK:
        if (!clockstatus::hasValidTime() || !halClock.formatTime(value, sizeof(value), SETTINGS.clockFormat == 1))
          snprintf(value, sizeof(value), "-");
        break;
      case S::STATUS_SLOT_BATTERY: {
        const int percent = std::clamp(static_cast<int>(powerManager.getDisplayedBatteryPercentage()), 0, 100);
        if (SETTINGS.batteryPercentShown(true)) snprintf(value, sizeof(value), "%d%%", percent);
        const int iconWidth = rough ? 44 : 26;
        const int block = iconWidth + (value[0] ? width(value) + 6 : 0);
        if (block > lane.width) continue;
        const int x = readerstatus::textX(lane, slot, block);
        if (rough) ugly::battery(r, x, y + 10, percent);
        else {
          r.drawRect(x, iconTop, 24, 14, 1, true);
          r.fillRect(x + 24, iconTop + 5, 2, 4);
          if (gpio.isUsbConnected()) drawChargingBolt(r, x, iconTop, 24, 14, false);
          else if (percent > 0) r.fillRect(x + 2, iconTop + 2, (20 * percent + 50) / 100, 10);
        }
        if (value[0]) text(x + iconWidth + 6, y, value);
        continue;
      }
      case S::STATUS_SLOT_CHAPTER_PAGES:
        if (pageCount > 0) snprintf(value, sizeof(value), "%s%d/%d", estimated ? "~" : "", currentPage, pageCount);
        else snprintf(value, sizeof(value), "-");
        break;
      case S::STATUS_SLOT_BOOK_PERCENT:
        if (bookProgress >= 0) snprintf(value, sizeof(value), "%.0f%%", std::clamp(bookProgress, 0.0f, 100.0f));
        else snprintf(value, sizeof(value), "-");
        break;
      case S::STATUS_SLOT_CHAPTER_ETA:
        readerstatus::duration(value, sizeof(value), chapterSeconds >= 0, static_cast<uint32_t>(chapterSeconds));
        break;
      case S::STATUS_SLOT_BOOK_ETA:
        readerstatus::duration(value, sizeof(value), bookSeconds >= 0, static_cast<uint32_t>(bookSeconds));
        break;
      default: continue;
    }
    const auto label = fit(value, lane.width);
    const int measured = width(label.c_str());
    if (measured <= lane.width) text(readerstatus::textX(lane, slot, measured), y, label.c_str());
  }
}

void tenorchrome::drawStatus(const GfxRenderer& r, const char* title, int currentPage, int pageCount,
                             float bookProgress, int paddingBottom, bool estimated, bool bookmarked,
                             bool titleIsName) {
  const bool trongTrinhDoc = title != nullptr;
  if (trongTrinhDoc && SETTINGS.statusBarSpec().slotsEnabled) {
    drawReaderSlots(r, title, currentPage, pageCount, bookProgress, estimated, bookmarked);
    return;
  }
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
  if (shell::uglyParts()) {
    // The same facts in hand: the battery drawn, the clock, and in a book the name and the counts between them.
    const int base = trongTrinhDoc ? statusTextY(r.getScreenHeight(), false, paddingBottom) + 18 : r.getScreenHeight() - 12;
    const bool clockLeft = SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
    int left = 14, right = width - 14;
    if (hienPin) {
      // In a book the number goes beside the battery when the reader asked for it.
      char pct[8] = "";
      if (trongTrinhDoc && hienPhanTram)
        snprintf(pct, sizeof(pct), "%d", std::max(0, std::min(100, static_cast<int>(powerManager.getDisplayedBatteryPercentage()))));
      const int room = 46 + (pct[0] ? ugly::width(r, ugly::Size::S22, pct) + 8 : 0);
      const int bx = clockLeft && hienGio ? right - room + 12 : left;
      ugly::battery(r, bx, base - 8, powerManager.getDisplayedBatteryPercentage());
      if (pct[0]) ugly::text(r, ugly::Size::S22, bx + 44, base, pct);
      if (bx == left) left += room;
      else right -= room;
    }
    char clock[12];
    if (hienGio && clockstatus::hasValidTime() && halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1)) {
      const int cw = ugly::width(r, ugly::Size::S22, clock);
      ugly::text(r, ugly::Size::S22, clockLeft ? left : right - cw, base, clock);
      if (clockLeft) left += cw + 12;
      else right -= cw + 12;
    }
    std::string counts;
    if (hienSoTrang) counts = (estimated ? "~" : "") + std::to_string(currentPage) + "/" + std::to_string(pageCount);
    if (hienTienDo) counts += (counts.empty() ? "" : "  ") + std::to_string(static_cast<int>(std::max(0.0f, std::min(100.0f, bookProgress)) + 0.5f)) + "%";
    if (!counts.empty()) {
      right -= ugly::width(r, ugly::Size::S22, counts.c_str());
      ugly::text(r, ugly::Size::S22, right, base, counts.c_str());
      right -= 12;
    }
    if (bookmarked) {  // a star in 5 pen strokes
      static constexpr int8_t STAR[6][2] = {{0, -9}, {5, 7}, {-8, -3}, {8, -3}, {-5, 7}, {0, -9}};
      const int cx = left + 9, cy = base - 8;
      for (int i = 0; i < 5; ++i)
        ugly::line(r, cx + STAR[i][0], cy + STAR[i][1], cx + STAR[i + 1][0], cy + STAR[i + 1][1], 870 + i, 1);
      left += 24;
    }
    if (hienTieuDe && right - left > 40) {
      const std::string name = ugly::fit(r, ugly::Size::S22, title, right - left);
      ugly::text(r, ugly::Size::S22, left, base, name.c_str());
    }
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "part=status reader=%d pct=%d mark=%d", trongTrinhDoc, trongTrinhDoc && hienPin && hienPhanTram, bookmarked);
#endif
    return;
  }
  const bool lon = !trongTrinhDoc && SETTINGS.globalStatusBarLarge();
  const int batteryWidth = lon ? 32 : 26;
  const int batteryHeight = lon ? 18 : 14;
  const int fontChu = lon ? UI_12_FONT_ID : SMALL_FONT_ID;
  // Touch shell: outside the reader the clock and the battery sit on the header row, battery rightmost;
  // the foot of the screen belongs to the tab bar.
  const bool top = kTouchShell && !trongTrinhDoc;
  const int y = top ? TOUCH_STATUS_TOP_INSET + HEADER_TOP + (headerHeight() - r.getLineHeight(fontChu)) / 2
                    : statusTextY(r.getScreenHeight(), lon, paddingBottom);
  const bool swap = SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
  char clock[12] = "--:--";
  halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1);
  const int timeWidth = r.getTextWidth(fontChu, clock);
  const int by = top ? y + STATUS_ICON_TOP_OFFSET : statusIconTopY(r.getScreenHeight(), lon, paddingBottom);
  const int percent = std::max(0, std::min(100, static_cast<int>(powerManager.getDisplayedBatteryPercentage())));
  char percentage[8];
  snprintf(percentage, sizeof(percentage), "%d", percent);
  const int batteryTextWidth = hienPhanTram ? r.getTextWidth(fontChu, percentage) : 0;
  const int batteryBlock = batteryWidth + (hienPhanTram ? BATTERY_TEXT_GAP + batteryTextWidth : 0);
  // Pin nam o ben DOI dien voi dong ho (hoac ben trai khi khong hien dong ho), nen
  // muc 5 (ten chuong & pin) khong day pin sang phai nhu khi vang dong ho.
  const bool batteryRight = top || (hienGio && swap);
  const int batteryBlockX = batteryRight ? width - STATUS_CORNER_INSET - batteryBlock : STATUS_CORNER_INSET;
  if (hienGio)
    r.drawText(fontChu,
               top    ? STRIP_LEFT
               : swap ? STATUS_CORNER_INSET
                      : width - STATUS_CORNER_INSET - timeWidth,
               y, clock);
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
  if (top) {
    HeaderBackTapTarget::strip = true;
    drawStripMiddle(r, y, hienGio ? STRIP_LEFT + timeWidth : STRIP_LEFT, batteryBlockX, fontChu);
    return;
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

// The same stroke as drawMoreChevron, every other pixel: grey like an unselected card icon.
void tenorchrome::drawRowChevron(const GfxRenderer& r, const int x, const int y) {
  constexpr int span = ROW_CHEVRON_SPAN;
  const int depth = moreChevronLength(span) - MORE_CHEVRON_STROKE;
  for (int t = -span; t <= span; ++t) {
    const int a = depth - ((t < 0 ? -t : t) * depth * 2 + span) / (2 * span);
    for (int i = 0; i < MORE_CHEVRON_STROKE; ++i)
      if (((x + a + i + y + span + t) & 1) == 0) r.drawPixel(x + a + i, y + span + t, true);
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

namespace {
// Bayer 8x8: a pixel of a band is cleared when its threshold reaches what the band keeps at its depth.
constexpr uint8_t BAYER8[8][8] = {{0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26},
                                  {12, 44, 4, 36, 14, 46, 6, 38},  {60, 28, 52, 20, 62, 30, 54, 22},
                                  {3, 35, 11, 43, 1, 33, 9, 41},   {51, 19, 59, 27, 49, 17, 57, 25},
                                  {15, 47, 7, 39, 13, 45, 5, 37},  {63, 31, 55, 23, 61, 29, 53, 21}};
}  // namespace

void tenorchrome::fadeBand(const GfxRenderer& r, const int y0, const int h, const bool outerTop, const int x0,
                           const int x1) {
  if (h <= 0) return;
  const int right = x1 < 0 ? r.getScreenWidth() : x1;
  for (int y = y0; y < y0 + h; ++y) {
    const int depth = outerTop ? y0 + h - 1 - y : y - y0;  // from the inner edge
    const int keep = 64 * (h - depth) / h;
    for (int x = x0; x < right; ++x)
      if (BAYER8[x & 7][y & 7] >= keep) r.drawPixel(x, y, false);
  }
}

int tenorchrome::smallFooterSymbolsTopY(const GfxRenderer& renderer) {
  return statusIconTopY(renderer.getScreenHeight(), false) - 1;
}

bool tenorchrome::compactFooterTips(const bool hasTextHints) {
  return !kTouchShell && enabled() && SETTINGS.tenorButtonSymbols && !SETTINGS.globalStatusBarHidden() &&
         !SETTINGS.globalStatusBarLarge() && !hasTextHints;
}

int tenorchrome::tipY(const GfxRenderer& renderer, const bool hasTextHints) {
  if (kTouchShell) return touchBarTop(renderer.getScreenHeight()) - 8 - renderer.getLineHeight(SMALL_FONT_ID);
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
int tenorchrome::uglyTipTop(const GfxRenderer& renderer, const int lines, const int linesAbove) {
  // The last line's baseline stands 14 px over the key bar, the lines 26 px apart.
  const int bottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight - 14;
  return bottom - (lines - 1 + linesAbove) * 26 - ugly::ascent(ugly::Size::S22);
}

void tenorchrome::drawTip(const GfxRenderer& renderer, const char* text, int linesAbove, int maxLines,
                          const bool hasTextHints) {
  // Global status-bar Off also hides contextual footer tips.
  if (SETTINGS.globalStatusBarHidden() || !tipShown(text)) return;
  if (shell::uglyParts()) {
    // Written in hand above the key bar, its key symbols drawn as the marks over the keys.
    const int w = renderer.getScreenWidth();
    const int lines = ugly::paragraph(renderer, ugly::Size::S22, 24, 0, w - 48, 26, text, false);
    ugly::paragraph(renderer, ugly::Size::S22, 24, uglyTipTop(renderer, lines, linesAbove) + ugly::ascent(ugly::Size::S22),
                    w - 48, 26, text);
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "part=tip");
#endif
    return;
  }
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
