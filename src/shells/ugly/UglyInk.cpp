#include "UglyInk.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "ClockStatus.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "UIFontTiers.h"
#include "UglyLogic.h"
#include "UglyTables.h"
#include "fontIds.h"
#include "fonts/ugly_22.h"
#include "fonts/ugly_30.h"
#include "fonts/ugly_38.h"
#include "fonts/ugly_52.h"

namespace ugly {
namespace {
constexpr int FONT_IDS[4] = {0x75676c32, 0x75676c33, 0x75676c34, 0x75676c35};
constexpr int PIXELS[4] = {22, 30, 38, 52};
// Ink above the baseline of a capital with its mark, per size, for placing boxes.
constexpr int ASCENT[4] = {20, 28, 36, 48};
const EpdFont FONT22(&ugly_22), FONT30(&ugly_30), FONT38(&ugly_38), FONT52(&ugly_52);

int idOf(const Size s) { return FONT_IDS[static_cast<int>(s)]; }
int pixelsOf(const Size s) { return PIXELS[static_cast<int>(s)]; }

int encode(const uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    out[1] = 0;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    out[2] = 0;
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    out[3] = 0;
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  out[4] = 0;
  return 4;
}

bool covered(const GfxRenderer& r, const int fid, const char* utf8) {
  const auto it = r.getFontMap().find(fid);
  if (it == r.getFontMap().end()) return false;
  const auto* p = reinterpret_cast<const unsigned char*>(utf8);
  while (const uint32_t cp = utf8NextCodepoint(&p))
    if (!it->second.hasCodepoint(cp)) return false;
  return true;
}

int fallbackFont(const Size s) { return s == Size::S22 || s == Size::S30 ? UI_12_FONT_ID : UI_TITLE_FONT_ID; }

// One pass for drawing and measuring, so the two cannot disagree.
int run(const GfxRenderer& r, const Size s, const int x, const int baseline, const std::string& text, const bool black,
        const bool draw) {
  const int fid = idOf(s);
  if (!covered(r, fid, text.c_str())) {
    const int fb = fallbackFont(s);
    if (draw) r.drawText(fb, x, baseline - r.getFontAscenderSize(fb), text.c_str(), black);
    return r.getTextAdvanceX(fb, text.c_str(), EpdFontFamily::REGULAR);
  }
  const int px = pixelsOf(s);
  const int top = baseline - r.getFontAscenderSize(fid);
  int cursor = x, pos = 0;
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  char one[5];
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    encode(cp, one);
    if (draw && cp != ' ') r.drawText(fid, cursor, top + logic::jumpDy(pos, cp, px), one, black);
    cursor += r.getTextAdvanceX(fid, one, EpdFontFamily::REGULAR) + logic::jumpStep(pos, cp);
    ++pos;
  }
  return cursor - x;
}

// A pen dot of width w centred near (x, y).
void dot(const GfxRenderer& r, const int x, const int y, const int w) {
  if (w <= 1) {
    r.drawPixel(x, y);
  } else if (w == 2) {
    r.drawPixel(x, y);
    r.drawPixel(x + 1, y);
    r.drawPixel(x, y + 1);
    r.drawPixel(x + 1, y + 1);
  } else {
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx)
        if (dx == 0 || dy == 0) r.drawPixel(x + dx, y + dy);
  }
}

// Bresenham with the pen dot at every step.
void stroke(const GfxRenderer& r, int x0, int y0, const int x1, const int y1, const int w) {
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int dy = y1 > y0 ? y1 - y0 : y0 - y1;
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  for (;;) {
    dot(r, x0, y0, w);
    if (x0 == x1 && y0 == y1) return;
    const int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void polyline(const GfxRenderer& r, const int (*pts)[2], const int n, const int w) {
  for (int i = 0; i + 1 < n; ++i) stroke(r, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], w);
}
}  // namespace

void ensureFonts(GfxRenderer& r) {
  if (r.getFontMap().count(FONT_IDS[0])) return;
  r.insertFont(FONT_IDS[0], EpdFontFamily(&FONT22));
  r.insertFont(FONT_IDS[1], EpdFontFamily(&FONT30));
  r.insertFont(FONT_IDS[2], EpdFontFamily(&FONT38));
  r.insertFont(FONT_IDS[3], EpdFontFamily(&FONT52));
}

int text(const GfxRenderer& r, const Size s, const int x, const int baseline, const char* utf8, const bool black) {
  return run(r, s, x, baseline, utf8ComposeNfc(utf8), black, true);
}

int width(const GfxRenderer& r, const Size s, const char* utf8) { return run(r, s, 0, 0, utf8ComposeNfc(utf8), true, false); }

int ascent(const Size s) { return ASCENT[static_cast<int>(s)]; }

std::string fit(const GfxRenderer& r, const Size s, const std::string& utf8, const int maxWidth) {
  std::string out = utf8ComposeNfc(utf8);
  const int fid = idOf(s);
  if (covered(r, fid, out.c_str()) && covered(r, fid, "...")) {
    // The baked font draws every cut of this line, and its advances add up character by character:
    // measure once, cut once.
    const int keep = logic::ellipsisKeep(out.c_str(), maxWidth, [&](const int pos, const uint32_t cp) {
      char one[5];
      encode(cp, one);
      return r.getTextAdvanceX(fid, one, EpdFontFamily::REGULAR) + logic::jumpStep(pos, cp);
    });
    if (keep == -1) return out;
    if (keep < 0) return std::string();
    out.resize(static_cast<size_t>(keep));
    return out + "...";
  }
  // A character the baked font lacks puts the line in the UI font, which is measured whole: shave and measure.
  if (width(r, s, out.c_str()) <= maxWidth) return out;
  while (!out.empty()) {
    utf8RemoveLastChar(out);
    const std::string cut = out + "...";
    if (width(r, s, cut.c_str()) <= maxWidth) return cut;
  }
  return out;
}

int paragraph(const GfxRenderer& r, const Size s, const int x, const int baseline, const int maxWidth, const int lineHeight,
              const char* utf8) {
  const std::string composed = utf8ComposeNfc(utf8);
  std::vector<std::string> words;
  size_t from = 0;
  while (from < composed.size()) {
    const size_t blank = composed.find(' ', from);
    words.push_back(composed.substr(from, blank == std::string::npos ? std::string::npos : blank - from));
    if (blank == std::string::npos) break;
    from = blank + 1;
  }
  std::vector<logic::Token> tokens;
  for (const auto& word : words) tokens.push_back({word.c_str(), 0, false});
  std::vector<logic::Placed> placed(tokens.size());
  const int lines = logic::layout(tokens.data(), static_cast<int>(tokens.size()), maxWidth, width(r, s, "a") / 2 + 4,
                                  [&](const char* t) { return width(r, s, t); }, placed.data());
  for (size_t i = 0; i < tokens.size(); ++i) text(r, s, x + placed[i].x, baseline + placed[i].line * lineHeight, tokens[i].text);
  return lines;
}

void circle(const GfxRenderer& r, const Circle role, const Box& box, const int padX, const int padY, const int w) {
  const CirclePoint* pts = role == Circle::Word ? CIRCLE_WORD : role == Circle::Object ? CIRCLE_OBJECT : CIRCLE_ROW;
  const int n = role == Circle::Word ? CIRCLE_WORD_COUNT : role == Circle::Object ? CIRCLE_OBJECT_COUNT : CIRCLE_ROW_COUNT;
  const int cx = (box.x0 + box.x1) / 2, cy = (box.y0 + box.y1) / 2;
  const int rx = (box.x1 - box.x0) / 2 + padX, ry = (box.y1 - box.y0) / 2 + padY;
  int px = 0, py = 0;
  for (int i = 0; i < n; ++i) {
    const int ux = rx * pts[i].x / 256;
    const int x = cx + ux, y = cy + ry * pts[i].y / 256 - ux / 25;  // a hand drawn circle leans a little
    if (i > 0) stroke(r, px, py, x, y, w);
    px = x;
    py = y;
  }
}

void underline(const GfxRenderer& r, const int x0, const int x1, const int y, const uint32_t seed, const int w) {
  constexpr int STEP = 26;
  const int a = x0 - 3, b = x1 + 6, len = std::max(1, b - a);
  const int n = std::min(14, std::max(1, len / STEP));
  int px = a, py = y + 1;
  for (int i = 1; i <= n; ++i) {
    const int x = a + len * i / n;
    int yy = y + 1 - 3 * i / n;  // a little uphill
    if (i < n) yy += logic::wobble(seed, i, 1);
    stroke(r, px, py, x, yy, w);
    px = x;
    py = yy;
  }
}

void line(const GfxRenderer& r, const int x0, const int y0, const int x1, const int y1, const uint32_t seed, const int w) {
  constexpr int STEP = 60;
  const int len = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
  const int n = std::min(20, std::max(1, len / STEP));
  int px = x0, py = y0;
  for (int i = 1; i <= n; ++i) {
    int x = x0 + (x1 - x0) * i / n, y = y0 + (y1 - y0) * i / n;
    if (i < n) x += logic::wobble(seed, i, 1);
    stroke(r, px, py, x, y, w);
    px = x;
    py = y;
  }
}

void tick(const GfxRenderer& r, const int x, const int y) {
  stroke(r, x - 12, y - 10, x, y, 3);
  stroke(r, x, y, x + 20, y - 24, 3);
}

void battery(const GfxRenderer& r, const int x, const int y, const int percent) {
  const int body[5][2] = {{x + 1, y - 9}, {x + 33, y - 10}, {x + 34, y + 8}, {x, y + 9}, {x + 1, y - 9}};
  polyline(r, body, 5, 2);
  stroke(r, x + 35, y - 3, x + 39, y - 2, 2);
  stroke(r, x + 39, y - 2, x + 39, y + 3, 2);
  stroke(r, x + 39, y + 3, x + 35, y + 3, 2);
  const int level = std::clamp(percent, 0, 100);
  for (int px = x + 4; px < x + 4 + 26 * level / 100; px += 3) stroke(r, px, y - 6, px + 1, y + 6, 1);
}

namespace {
void glyph(const GfxRenderer& r, const char kind, const int cx, const int cy) {
  switch (kind) {
    case 'b': {  // back: an arrow that turns round
      const int a[4][2] = {{cx + 9, cy + 7}, {cx + 10, cy - 3}, {cx + 4, cy - 7}, {cx - 9, cy - 6}};
      polyline(r, a, 4, 2);
      stroke(r, cx - 3, cy - 12, cx - 10, cy - 6, 2);
      stroke(r, cx - 10, cy - 6, cx - 3, cy - 1, 2);
      break;
    }
    case 'l':
      stroke(r, cx + 4, cy - 9, cx - 5, cy, 2);
      stroke(r, cx - 5, cy, cx + 4, cy + 9, 2);
      break;
    case 'r':
      stroke(r, cx - 4, cy - 9, cx + 5, cy, 2);
      stroke(r, cx + 5, cy, cx - 4, cy + 9, 2);
      break;
    case 'c':
      stroke(r, cx - 9, cy - 1, cx - 3, cy + 7, 2);
      stroke(r, cx - 3, cy + 7, cx + 10, cy - 10, 2);
      break;
    default:
      break;
  }
}
}  // namespace

void statusBar(const GfxRenderer& r, const MappedInputManager& input, const Hints hints) {
  const int w = r.getScreenWidth(), h = r.getScreenHeight();
  const int y = h - 20;
  battery(r, 14, y, powerManager.getDisplayedBatteryPercentage());
  char clock[10];
  if (SETTINGS.clockShowInHeader && clockstatus::hasValidTime() && halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1))
    text(r, Size::S22, w - 14 - width(r, Size::S22, clock), y + 8, clock);
  const auto labels = input.mapLabels(hints.back ? "b" : "", hints.confirm ? "c" : "", hints.left ? "l" : "", hints.right ? "r" : "");
  static constexpr int WIDE[4] = {105, 197, 331, 423}, NARROW[4] = {98, 186, 294, 382};
  const int* centres = w >= 528 ? WIDE : NARROW;
  const char* marks[4] = {labels.btn1, labels.btn2, labels.btn3, labels.btn4};
  for (int i = 0; i < 4; ++i)
    if (marks[i] && marks[i][0]) glyph(r, marks[i][0], centres[i], y);
}

}  // namespace ugly
