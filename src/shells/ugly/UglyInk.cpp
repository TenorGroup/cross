#include "UglyInk.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ClockStatus.h"
#include "CrossPointSettings.h"
#include "Epub/converters/DirectPixelWriter.h"
#include "MappedInputManager.h"
#include "UIFontTiers.h"
#include "UglyLogic.h"
#include "UglyTables.h"
#if FREEINK_DEVICE_X4PRO
#include "UglyTouch.h"
#endif
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
const EpdFont* const FONTS[4] = {&FONT22, &FONT30, &FONT38, &FONT52};

// A chapter title keeps this much clear at each side, and this much below its last line for the underline.
constexpr int TITLE_SIDE = 8, TITLE_UNDERLINE_ROOM = 5;

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

// "ugly af" turns, shrinks and lifts every letter as it is drawn and lets the line jump; "ugly" draws the straight baked
// letters as they are. The setting is read here, at draw time, so a change shows on the next screen.
bool wild() { return logic::levelOf(SETTINGS.uiUglyLevel) == logic::Level::Af; }

struct Letter {
  const EpdGlyph* glyph;
  logic::Warp warp;
};

Letter letterOf(const Size s, const bool af, const uint32_t cp) {
  const EpdGlyph* glyph = af && cp != ' ' ? FONTS[static_cast<int>(s)]->getGlyph(cp) : nullptr;
  return {glyph, glyph ? logic::warpOf(cp, pixelsOf(s)) : logic::NO_WARP};
}

// Advance in pixels of one letter. Drawing and measuring ask this and nothing else, so the two cannot disagree.
int stepOf(const GfxRenderer& r, const Size s, const bool af, const int pos, const uint32_t cp, const Letter& letter) {
  if (letter.glyph) return logic::warpAdvance(letter.glyph->advanceX, letter.warp) + logic::jumpStep(pos, cp);
  char one[5];
  encode(cp, one);
  return r.getTextAdvanceX(idOf(s), one, EpdFontFamily::REGULAR) + (af ? logic::jumpStep(pos, cp) : 0);
}

// One straight glyph turned by its warp, its pen on (x, baseline). Its ink goes straight into the frame buffer, as
// the images do: a call of drawPixel for each of some 20000 pixels of a screen is what the baked letters did not cost.
void drawWarped(const GfxRenderer& r, const Size s, const Letter& letter, const int x, const int baseline, const bool black) {
  if (r.isFontCacheScanning()) return;  // the prewarm pass only collects what a page will draw
  const EpdFont& font = *FONTS[static_cast<int>(s)];
  const EpdGlyph* g = letter.glyph;
  if (!g) return;
  DirectPixelWriter pen;
  pen.init(const_cast<GfxRenderer&>(r));  // it only reads the renderer; its signature is the images', which hold a non-const one
  // The black and white pass on a whole frame: the frame buffer is one run of bits, so a pixel is one step along each axis.
  const int rowBits = pen.displayWidthBytes * 8;
  const int stepX = pen.phyYStepX * rowBits + pen.phyXStepX, stepY = pen.phyYStepY * rowBits + pen.phyXStepY;
  const int from = (pen.phyYBase + x * pen.phyYStepX + baseline * pen.phyYStepY) * rowBits + pen.phyXBase + x * pen.phyXStepX +
                   baseline * pen.phyXStepY;
  logic::warpGlyph(font.data->bitmap + g->dataOffset, g->width, g->height, g->left, g->top, g->advanceX, letter.warp,
                   {-x, -baseline, r.getScreenWidth() - x, r.getScreenHeight() - baseline}, [&](const int px, const int py) {
                     const int bit = from + px * stepX + py * stepY;
                     const uint8_t mask = 0x80 >> (bit & 7);
                     if (black)
                       pen.fb[bit >> 3] &= ~mask;
                     else
                       pen.fb[bit >> 3] |= mask;
                   });
}

// One pass for drawing and measuring, so the two cannot disagree.
int run(const GfxRenderer& r, const Size s, const int x, const int baseline, const std::string& text, const bool black,
        const bool draw) {
  const int fid = idOf(s);
  if (!covered(r, fid, text.c_str())) {
    const int fb = fallbackFont(s);
    if (draw) r.drawText(fb, x, baseline - r.getFontAscenderSize(fb), text.c_str(), black);
    return r.getTextAdvanceX(fb, text.c_str(), EpdFontFamily::REGULAR);
  }
  const bool af = wild();
  const int px = pixelsOf(s);
  const int top = baseline - r.getFontAscenderSize(fid);
  int cursor = x, pos = 0;
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  char one[5];
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    const Letter letter = letterOf(s, af, cp);
    if (draw && cp != ' ') {
      if (af) {
        drawWarped(r, s, letter, cursor, baseline + logic::jumpDy(pos, cp, px), black);
      } else {
        encode(cp, one);
        r.drawText(fid, cursor, top, one, black);
      }
    }
    cursor += stepOf(r, s, af, pos, cp, letter);
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
    const bool af = wild();
    const int keep = logic::ellipsisKeep(out.c_str(), maxWidth, [&](const int pos, const uint32_t cp) {
      return stepOf(r, s, af, pos, cp, letterOf(s, af, cp));
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
              const char* utf8, const bool draw) {
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
  if (!draw) return lines;
  for (size_t i = 0; i < tokens.size(); ++i) text(r, s, x + placed[i].x, baseline + placed[i].line * lineHeight, tokens[i].text);
  return lines;
}

bool chapterTitle(const GfxRenderer& r, const char* utf8, const int x0, const int x1, const int top, const int bottom,
                  const bool draw) {
  const std::string title = utf8ComposeNfc(utf8);
  std::vector<std::string> words;
  for (size_t from = 0; from < title.size();) {
    const size_t blank = title.find(' ', from);
    if (blank != from) words.push_back(title.substr(from, blank == std::string::npos ? blank : blank - from));
    if (blank == std::string::npos) break;
    from = blank + 1;
  }
  if (words.empty()) return false;
  std::vector<logic::Token> tokens;
  for (const auto& word : words) tokens.push_back({word.c_str(), 0, false});
  const int maxWidth = x1 - x0 - 2 * TITLE_SIDE;
  for (const Size s : {Size::S38, Size::S30, Size::S22}) {
    if (!covered(r, idOf(s), title.c_str())) return false;
    std::vector<logic::Placed> placed(tokens.size());
    const int lines = logic::layout(tokens.data(), static_cast<int>(tokens.size()), maxWidth, width(r, s, "a") / 2 + 4,
                                    [&](const char* t) { return width(r, s, t); }, placed.data());
    const int px = pixelsOf(s), pitch = px * 5 / 4;
    const int need = ASCENT[static_cast<int>(s)] + (lines - 1) * pitch + px / 4 + TITLE_UNDERLINE_ROOM;
    int lineWidth[3] = {0, 0, 0};
    bool fits = lines <= 3 && need <= bottom - top;
    for (size_t i = 0; fits && i < placed.size(); ++i) {
      lineWidth[placed[i].line] = std::max(lineWidth[placed[i].line], placed[i].x + placed[i].w);
      fits = lineWidth[placed[i].line] <= maxWidth;
    }
    if (!fits) continue;
    if (!draw) return true;
    const int first = top + (bottom - top - need) / 2 + ASCENT[static_cast<int>(s)];
    for (size_t i = 0; i < tokens.size(); ++i) {
      const int line = placed[i].line;
      text(r, s, x0 + (x1 - x0 - lineWidth[line]) / 2 + placed[i].x, first + line * pitch, tokens[i].text);
    }
    const int last = first + (lines - 1) * pitch;
    const int left = x0 + (x1 - x0 - lineWidth[lines - 1]) / 2;
    underline(r, left, left + lineWidth[lines - 1], last + px / 4, static_cast<uint32_t>(title.size()));
    return true;
  }
  return false;
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
// 1/16 px to px, rounded away from zero.
int sixteenths(const int v) { return (v + (v < 0 ? -8 : 8)) / 16; }
}  // namespace

void mark(const GfxRenderer& r, const Mark m, const int cx, const int cy) {
  static constexpr const CirclePoint* TABLES[6] = {MARK_LEFT, MARK_RIGHT, MARK_UP, MARK_DOWN, MARK_TICK, MARK_BACK};
  static constexpr int COUNTS[6] = {MARK_LEFT_COUNT, MARK_RIGHT_COUNT, MARK_UP_COUNT, MARK_DOWN_COUNT, MARK_TICK_COUNT, MARK_BACK_COUNT};
  const auto* pts = TABLES[static_cast<int>(m)];
  const int n = COUNTS[static_cast<int>(m)];
  for (int i = 1; i < n; ++i)
    stroke(r, cx + sixteenths(pts[i - 1].x), cy + sixteenths(pts[i - 1].y), cx + sixteenths(pts[i].x), cy + sixteenths(pts[i].y), 2);
}

void pageHints(const GfxRenderer& r, const char* before, const char* after, const int baseline) {
  const int w = r.getScreenWidth();
  mark(r, Mark::Left, 32, baseline - 7);
  text(r, Size::S22, 52, baseline, before);
  mark(r, Mark::Right, w - 32, baseline - 7);
  text(r, Size::S22, w - 52 - width(r, Size::S22, after), baseline, after);
}

void statusBar(const GfxRenderer& r, const MappedInputManager& input, const Hints hints) {
  const int w = r.getScreenWidth(), h = r.getScreenHeight();
  const int y = h - 20;
  battery(r, 14, y, powerManager.getDisplayedBatteryPercentage());
  char clock[10];
  if (SETTINGS.clockShowInHeader && clockstatus::hasValidTime() && halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1))
    text(r, Size::S22, w - 14 - width(r, Size::S22, clock), y + 8, clock);
  // Front Left and Right walk up and down the screen, so their marks point up and down.
  const auto labels = input.mapLabels(hints.back ? "b" : "", hints.confirm ? "c" : "", hints.left ? "u" : "", hints.right ? "d" : "");
  static constexpr int WIDE[4] = {105, 197, 331, 423}, NARROW[4] = {98, 186, 294, 382};
  const int* centres = w >= 528 ? WIDE : NARROW;
  const char* marks[4] = {labels.btn1, labels.btn2, labels.btn3, labels.btn4};
  for (int i = 0; i < 4; ++i)
    if (marks[i] && marks[i][0]) mark(r, marks[i][0] == 'b' ? Mark::Back : marks[i][0] == 'c' ? Mark::Tick : marks[i][0] == 'u' ? Mark::Up : Mark::Down, centres[i], y);
}

#if FREEINK_DEVICE_X4PRO
void topBar(const GfxRenderer& r, const char* left) {
  if (left && *left) text(r, Size::S22, 24, 34, fit(r, Size::S22, left, 300).c_str());
  constexpr int X0 = 412, X1 = 456, Y0 = 12, Y1 = 37;
  const int body[5][2] = {{X0, Y0 + 1}, {X1, Y0}, {X1 + 1, Y1}, {X0 - 1, Y1 + 1}, {X0, Y0 + 1}};
  polyline(r, body, 5, 2);
  stroke(r, X1 + 2, Y0 + 8, X1 + 6, Y0 + 8, 2);
  stroke(r, X1 + 6, Y0 + 8, X1 + 6, Y1 - 8, 2);
  stroke(r, X1 + 6, Y1 - 8, X1 + 2, Y1 - 8, 2);
  const int level = std::clamp(static_cast<int>(powerManager.getDisplayedBatteryPercentage()), 0, 100);
  char pct[6];
  snprintf(pct, sizeof(pct), "%d", level);
  const int pw = width(r, Size::S22, pct);
  if (pw <= X1 - X0 - 6) {
    text(r, Size::S22, (X0 + X1) / 2 - pw / 2, Y1 - 5, pct);
  } else {  // no room for the number: the level in pen strokes, as on the X3
    for (int px = X0 + 3; px < X0 + 3 + (X1 - X0 - 6) * level / 100; px += 3) stroke(r, px, Y0 + 4, px + 1, Y1 - 4, 1);
  }
  char clock[10];
  if (SETTINGS.clockShowInHeader && clockstatus::hasValidTime() && halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1))
    text(r, Size::S22, 400 - width(r, Size::S22, clock), 34, clock);
}

void formTopBar(const GfxRenderer& r) {
  if (SETTINGS.globalStatusBarHidden()) return;
  const int x0 = r.getScreenWidth() - 68, x1 = r.getScreenWidth() - 24;
  constexpr int y0 = 12, y1 = 37;
  const int body[5][2] = {{x0, y0 + 1}, {x1, y0}, {x1 + 1, y1}, {x0 - 1, y1 + 1}, {x0, y0 + 1}};
  polyline(r, body, 5, 2);
  stroke(r, x1 + 2, y0 + 8, x1 + 6, y0 + 8, 2);
  stroke(r, x1 + 6, y0 + 8, x1 + 6, y1 - 8, 2);
  stroke(r, x1 + 6, y1 - 8, x1 + 2, y1 - 8, 2);
  const int level = std::clamp(static_cast<int>(powerManager.getDisplayedBatteryPercentage()), 0, 100);
  char pct[6];
  snprintf(pct, sizeof(pct), "%d", level);
  const int pw = width(r, Size::S22, pct);
  if (pw <= x1 - x0 - 6) text(r, Size::S22, (x0 + x1 - pw) / 2, y1 - 5, pct);
  else for (int x = x0 + 3; x < x0 + 3 + (x1 - x0 - 6) * level / 100; x += 3) stroke(r, x, y0 + 4, x + 1, y1 - 4, 1);
  char clock[10];
  if (SETTINGS.clockShowInHeader && clockstatus::hasValidTime() && halClock.formatTime(clock, sizeof(clock), SETTINGS.clockFormat == 1))
    text(r, Size::S22, 24, 34, clock);
}

void arrow(const GfxRenderer& r, const int x, const int y, const bool down, const int length) {
  const int s = down ? 1 : -1;
  stroke(r, x, y - s * length / 2, x + 1, y + s * length / 2, 2);
  stroke(r, x - 7, y + s * (length / 2 - 8), x + 1, y + s * length / 2, 2);
  stroke(r, x + 1, y + s * length / 2, x + 8, y + s * (length / 2 - 9), 2);
}

void navRow(const GfxRenderer& r, const char* prev, const char* back, const char* next) {
  constexpr int BASE = 758;
  if (prev && *prev) text(r, Size::S30, 16, BASE, (std::string("< ") + prev).c_str());
  if (back && *back) {
    text(r, Size::S22, 240 - width(r, Size::S22, back) / 2, BASE - 6, back);
    arrow(r, 240, BASE + 12, true, 18);
  }
  if (next && *next) {
    const std::string after = std::string(next) + " >";
    text(r, Size::S30, 464 - width(r, Size::S30, after.c_str()), BASE, after.c_str());
  }
}

namespace {
// A torn edge: a zigzag across the paper at y.
void torn(const GfxRenderer& r, const int y, const uint32_t seed) {
  int px = 24, py = y - 5;
  for (int k = 1, x = 38; x <= 468; ++k, x += 14) {
    const int yy = y + (k % 2 ? 5 : -5) + logic::wobble(seed, k, 1);
    stroke(r, px, py, x, yy, 2);
    px = x;
    py = yy;
  }
}
}  // namespace

void paper(const GfxRenderer& r, const int top, const int bottom, const bool tornTop, const bool tornBottom, const uint32_t seed) {
  const int e0 = touch::rubEdge(top - 12, true), e1 = touch::rubEdge(bottom + 12, false);
  r.fillRect(8, std::max(0, e0), 466, std::min(800, e1) - std::max(0, e0), false);
  if (tornTop) torn(r, top, seed);
  else line(r, 24, top + 2, 466, top - 3, seed, 2);
  if (tornBottom) torn(r, bottom, seed + 1);
  else line(r, 26, bottom + 3, 468, bottom, seed + 1, 2);
  line(r, 466, top - 3, 468, bottom, seed + 2, 2);
  line(r, 24, top + 2, 26, bottom + 3, seed + 3, 2);
  if (!tornBottom) stroke(r, 442, bottom + 1, 467, bottom - 24, 1);  // a folded corner
  if (!tornTop) {  // the clip
    const int clip[5][2] = {{214, top + 12}, {266, top + 11}, {260, top - 8}, {220, top - 7}, {214, top + 12}};
    polyline(r, clip, 5, 2);
    const int a[4][2] = {{224, top - 7}, {228, top - 22}, {240, top - 25}, {236, top - 8}};
    const int b[4][2] = {{244, top - 8}, {250, top - 23}, {258, top - 20}, {256, top - 7}};
    polyline(r, a, 4, 2);
    polyline(r, b, 4, 2);
  }
}

void tickBox(const GfxRenderer& r, const int x, const int y, const bool ticked) {
  const int box[5][2] = {{x - 22, y - 10}, {x, y - 11}, {x + 1, y + 10}, {x - 23, y + 11}, {x - 22, y - 10}};
  polyline(r, box, 5, 2);
  if (ticked) {
    stroke(r, x - 18, y - 1, x - 12, y + 7, 3);
    stroke(r, x - 12, y + 7, x + 4, y - 16, 3);
  }
}

void heart(const GfxRenderer& r, const int x, const int y) {
  const int h[11][2] = {{x, y + 11}, {x - 9, y + 2}, {x - 11, y - 4}, {x - 8, y - 9}, {x - 3, y - 9}, {x, y - 4},
                        {x + 3, y - 9}, {x + 8, y - 10}, {x + 12, y - 4}, {x + 9, y + 3}, {x + 1, y + 11}};
  polyline(r, h, 11, 2);
}

void liftArt(const GfxRenderer& r, const uint8_t* plane, const Box& box, const int dx, const int dy) {
  // Row 527 - x, column y, a set bit is white (scripts/ugly/gen_art.py).
  const int x0 = std::max(0, box.x0), x1 = std::min(logic::FRAME_W - 1, box.x1);
  const int y0 = std::max(0, box.y0), y1 = std::min(logic::FRAME_H - 1, box.y1);
  for (int x = x0; x <= x1; ++x) {
    const uint8_t* row = plane + (logic::FRAME_W - 1 - x) * (logic::FRAME_H / 8);
    for (int y = y0; y <= y1; ++y)
      if (!(row[y / 8] & (0x80 >> (y % 8)))) r.drawPixel(x + dx, y + dy);
  }
}

void penPath(const GfxRenderer& r, const int16_t* xs, const int16_t* ys, const int n) {
  for (int i = 0; i + 1 < n; ++i) stroke(r, xs[i], ys[i], xs[i + 1], ys[i + 1], 3);
}
#endif

}  // namespace ugly
