#include "UglyInk.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#include "components/ButtonSymbols.h"
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

// Drawn by the pen, not taken from a font: the scrawl a line runs into when it is too long (fit() puts it where
// the dots would go), the tick of the Select key, which no font on the card has, and the inline key symbols of
// the UI strings (U+E100 Select, Back, Up, Down, Left, Right, then the star, the 2 side buttons and the erase key),
// drawn as the marks over the keys.
constexpr uint32_t SCRAWL = 0xE000, TICK = 0x2713, MARGIN_PIN = 0xE10A, KEY_FIRST = 0xE100, KEY_LAST = 0xE109;
bool penDrawn(const uint32_t cp) {
  return cp == MARGIN_PIN || cp == SCRAWL || cp == TICK || (cp >= KEY_FIRST && cp <= KEY_LAST);
}

// The baked pen has no degree sign: on the X4 Pro the pen draws it, a small ring, so "180°" stays in one hand.
constexpr uint32_t DEGREE = 0xB0;
#if FREEINK_DEVICE_X4PRO
constexpr bool PEN_DEGREE = true;
#else
constexpr bool PEN_DEGREE = false;
#endif
bool penDegree(const uint32_t cp) { return PEN_DEGREE && cp == DEGREE; }
int degreeRadius(const int px) { return px / 10 > 3 ? px / 10 : 3; }
void stroke(const GfxRenderer& r, int x0, int y0, int x1, int y1, int w);

bool covered(const GfxRenderer& r, const int fid, const char* utf8) {
  const auto it = r.getFontMap().find(fid);
  if (it == r.getFontMap().end()) return false;
  const auto* p = reinterpret_cast<const unsigned char*>(utf8);
  while (const uint32_t cp = utf8NextCodepoint(&p))
    if (!penDrawn(cp) && !penDegree(cp) && !it->second.hasCodepoint(cp)) return false;
  return true;
}

int fallbackFont(const Size s) { return s == Size::S22 || s == Size::S30 ? UI_12_FONT_ID : UI_TITLE_FONT_ID; }

// A letter the baked pen has (a Chinese one has not: it is written in the UI font, straight, in its place in the line).
bool baked(const Size s, const uint32_t cp) { return cp == ' ' || FONTS[static_cast<int>(s)]->hasCodepoint(cp); }

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
  if (cp == MARGIN_PIN) return 0;
  if (cp == SCRAWL) return 3 * stepOf(r, s, false, pos, '.', {nullptr, logic::NO_WARP});
  if (penDrawn(cp)) return 26;  // a mark is some 20 px across
  if (penDegree(cp)) return 2 * degreeRadius(pixelsOf(s)) + 4;
  char one[5];
  encode(cp, one);
  if (!baked(s, cp)) return r.getTextAdvanceX(fallbackFont(s), one, EpdFontFamily::REGULAR);
  if (letter.glyph) return logic::warpAdvance(letter.glyph->advanceX, letter.warp) + logic::jumpStep(pos, cp);
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

void stroke(const GfxRenderer& r, int x0, int y0, int x1, int y1, int w);

// A star of one hasty stroke, some 20 px across, centred on (cx, cy).
void penStar(const GfxRenderer& r, const int cx, const int cy) {
  static constexpr int PTS[6][2] = {{0, -10}, {6, 8}, {-10, -3}, {10, -4}, {-6, 8}, {1, -10}};
  for (int i = 0; i + 1 < 6; ++i)
    stroke(r, cx + PTS[i][0], cy + PTS[i][1], cx + PTS[i + 1][0], cy + PTS[i + 1][1], 2);
}

// The erase key, a tag with a cross in it, of one stroke and one more for the cross, some 20 px across.
void penErase(const GfxRenderer& r, const int cx, const int cy) {
  static constexpr int PTS[7][2] = {{-10, 0}, {-4, -8}, {10, -8}, {10, 8}, {-4, 8}, {-10, 1}, {-9, 0}};
  for (int i = 0; i + 1 < 7; ++i)
    stroke(r, cx + PTS[i][0], cy + PTS[i][1], cx + PTS[i + 1][0], cy + PTS[i + 1][1], 2);
  stroke(r, cx - 2, cy - 4, cx + 5, cy + 4, 2);
  stroke(r, cx - 2, cy + 4, cx + 5, cy - 3, 2);
}

// One key symbol of the UI strings, centred on (cx, cy). The star, the side buttons and the erase key are the keys'
// own, as buttonSymbols resolves them (the side buttons follow the reading side layout).
void keyMark(const GfxRenderer& r, const uint32_t cp, const int cx, const int cy) {
  static constexpr Mark KEYS[6] = {Mark::Tick, Mark::Back, Mark::Up, Mark::Down, Mark::Left, Mark::Right};
  if (cp == TICK) return mark(r, Mark::Tick, cx, cy);
  const int id = static_cast<int>(cp - KEY_FIRST);
  if (id < 6) return mark(r, KEYS[id], cx, cy);
  switch (buttonSymbols::resolve(id).shape) {
    case inlineSymbols::Shape::Star:
      return penStar(r, cx, cy);
    case inlineSymbols::Shape::Erase:
      return penErase(r, cx, cy);
    case inlineSymbols::Shape::Left:
      return mark(r, Mark::Left, cx, cy);
    case inlineSymbols::Shape::Right:
      return mark(r, Mark::Right, cx, cy);
    default:
      return;
  }
}

// The pen marks that stand in a line of text: a hasty scrawl over [x, x + advance), or a key mark centred in it.
void penMark(const GfxRenderer& r, const Size s, const uint32_t cp, const int x, const int baseline, const int advance) {
  const int h = ASCENT[static_cast<int>(s)];
  if (cp != SCRAWL) {
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "part=penmark cp=%X", static_cast<unsigned>(cp));
#endif
    if (cp == MARGIN_PIN) {
      const int size = inlineSymbols::marginPinHeight() == 8 ? 14 : 20;
      const auto clip = r.getClipRect();
      r.setClipRect(0, clip[1], clip[0] + clip[2], clip[3]);
      heart(r, 20 - size / 2, baseline - h / 2, size);
      r.setClipRect(clip[0], clip[1], clip[2], clip[3]);
    } else {
      keyMark(r, cp, x + advance / 2, baseline - h / 2);
    }
    return;
  }
  // Loops of a pen that gave up writing: up and down a little under the x-height, each step a little off.
  int px = x, py = baseline - h / 4;
  for (int i = 1, cx = x + 4; cx <= x + advance; ++i, cx += 4) {
    const int cy = baseline - (i % 2 ? h * 3 / 5 : h / 5) + logic::wobble(977, i, 1);
    stroke(r, px, py, cx, cy, 2);
    px = cx;
    py = cy;
  }
}

// One pass for drawing and measuring, so the two cannot disagree.
int run(const GfxRenderer& r, const Size s, const int x, const int baseline, const std::string& text, const bool black,
        const bool draw, const bool straight) {
  const int fid = idOf(s), fb = fallbackFont(s);
  const bool af = !straight && wild();
  const int px = pixelsOf(s);
  const int top = baseline - r.getFontAscenderSize(fid);
  int cursor = x, pos = 0;
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  char one[5];
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    const Letter letter = letterOf(s, af, cp);
    if (penDrawn(cp)) {
      const int advance = stepOf(r, s, af, pos, cp, letter);
      if (draw && !r.isFontCacheScanning()) penMark(r, s, cp, cursor, baseline, advance);
      cursor += advance;
      if (cp != MARGIN_PIN) ++pos;
      continue;
    }
    if (penDegree(cp)) {
      if (draw) {
        // An octagon of pen strokes at the height of the capitals' top.
        const int rr = degreeRadius(px), cx = cursor + rr + 1, cy = baseline - ascent(s) + rr;
        const int d = rr * 7 / 10;
        const int ring[9][2] = {{0, -rr}, {d, -d}, {rr, 0}, {d, d}, {0, rr}, {-d, d}, {-rr, 0}, {-d, -d}, {0, -rr}};
        for (int i = 0; i < 8; ++i) stroke(r, cx + ring[i][0], cy + ring[i][1], cx + ring[i + 1][0], cy + ring[i + 1][1], 2);
      }
      cursor += stepOf(r, s, af, pos, cp, letter);
      ++pos;
      continue;
    }
    if (!baked(s, cp)) {  // the UI font for this letter alone, on the same baseline
      encode(cp, one);
      if (draw) r.drawText(fb, cursor, baseline - r.getFontAscenderSize(fb), one, black);
      cursor += stepOf(r, s, af, pos, cp, letter);
      ++pos;
      continue;
    }
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

int text(const GfxRenderer& r, const Size s, const int x, const int baseline, const char* utf8, const bool black,
         const bool straight) {
  return run(r, s, x, baseline, utf8ComposeNfc(utf8), black, true, straight);
}

int width(const GfxRenderer& r, const Size s, const char* utf8, const bool straight) {
  return run(r, s, 0, 0, utf8ComposeNfc(utf8), true, false, straight);
}

int ascent(const Size s) { return ASCENT[static_cast<int>(s)]; }

std::string fit(const GfxRenderer& r, const Size s, const std::string& utf8, const int maxWidth) {
  std::string out = utf8ComposeNfc(utf8);
  // Every letter has its own advance (a baked one, or the UI font's for a letter the pen lacks), and they add up
  // character by character: measure once, cut once.
  const bool af = wild();
  const int keep = logic::ellipsisKeep(out.c_str(), maxWidth, [&](const int pos, const uint32_t cp) {
    return stepOf(r, s, af, pos, cp, letterOf(s, af, cp));
  });
  if (keep == -1) return out;
  if (keep < 0) return std::string();
  out.resize(static_cast<size_t>(keep));
  return out + "\xEE\x80\x80";  // SCRAWL: the line trails off in the pen, no dots
}

int paragraph(const GfxRenderer& r, const Size s, const int x, const int baseline, const int maxWidth, const int lineHeight,
              const char* utf8, const bool draw, const bool straight) {
  const std::string composed = utf8ComposeNfc(utf8);
  std::vector<std::string> words;
  size_t from = 0;
  while (from < composed.size()) {
    const size_t blank = composed.find(' ', from);
    words.push_back(composed.substr(from, blank == std::string::npos ? std::string::npos : blank - from));
    if (blank == std::string::npos) break;
    from = blank + 1;
  }
  // A line break of the sentence ends the line there (a null token), the words on either side kept apart.
  std::vector<std::string> pieces;
  for (const auto& word : words)
    for (size_t at = 0;;) {
      const size_t nl = word.find('\n', at);
      pieces.push_back(word.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
      if (nl == std::string::npos) break;
      pieces.push_back("\n");
      at = nl + 1;
    }
  std::vector<logic::Token> tokens;
  for (const auto& piece : pieces)
    if (piece == "\n") tokens.push_back({nullptr, 0, false});
    else if (!piece.empty()) tokens.push_back({piece.c_str(), 0, false});
  std::vector<logic::Placed> placed(tokens.size());
  const int lines = logic::layout(tokens.data(), static_cast<int>(tokens.size()), maxWidth, width(r, s, "a", straight) / 2 + 4,
                                  [&](const char* t) { return width(r, s, t, straight); }, placed.data());
  if (!draw) return lines;
  for (size_t i = 0; i < tokens.size(); ++i)
    if (tokens[i].text) text(r, s, x + placed[i].x, baseline + placed[i].line * lineHeight, tokens[i].text, true, straight);
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

namespace {
// The one tick of the shell (founder 06/10/2026): 2 hasty strokes, the short one down to the elbow, the long one up
// from it, each end and the elbow a little off. The wobble is seeded by where the tick stands, so a tick drawn
// again at its place is the same pixels (no e-ink flicker) and a tick elsewhere is a little different.
void penTick(const GfxRenderer& r, const int x, const int y, const int span, const int width) {
  const uint32_t seed = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u;
  const int jiggle = std::max(1, span / 10);
  const int ex = x + logic::wobble(seed, 0, jiggle), ey = y + logic::wobble(seed, 1, jiggle);
  stroke(r, ex - span * 3 / 8 + logic::wobble(seed, 2, jiggle), ey - span * 5 / 16 + logic::wobble(seed, 3, jiggle), ex, ey, width);
  stroke(r, ex, ey, ex + span * 5 / 8 + logic::wobble(seed, 4, jiggle), ey - span * 3 / 4 + logic::wobble(seed, 5, jiggle), width);
}
}  // namespace

void tick(const GfxRenderer& r, const int x, const int y) { penTick(r, x, y, 32, 3); }

namespace {
void batteryDigits(const GfxRenderer& renderer, const int batteryX, const int batteryY, const int level) {
  char digits[4];
  snprintf(digits, sizeof(digits), "%d", level);
  constexpr logic::Warp small = {0, 4096, 65, 0, 100};
  int minX = 100, minY = 100, maxX = -100, maxY = -100, cursor = 0;
  for (const char* digit = digits; *digit; ++digit) {
    const auto* glyph = FONT22.getGlyph(*digit);
    logic::warpGlyph(FONT22.data->bitmap + glyph->dataOffset, glyph->width, glyph->height, glyph->left, glyph->top,
                     glyph->advanceX, small, logic::NO_CLIP, [&](const int px, const int py) {
                       minX = std::min(minX, cursor + px);
                       maxX = std::max(maxX, cursor + px);
                       minY = std::min(minY, py);
                       maxY = std::max(maxY, py);
                     });
    cursor += logic::warpAdvance(glyph->advanceX, small) + 1;
  }
  cursor = batteryX + 17 - (minX + maxX) / 2;
  const int baseline = batteryY - (minY + maxY) / 2;
  for (const char* digit = digits; *digit; ++digit) {
    const auto* glyph = FONT22.getGlyph(*digit);
    drawWarped(renderer, Size::S22, {glyph, small}, cursor, baseline, true);
    cursor += logic::warpAdvance(glyph->advanceX, small) + 1;
  }
}
}

void battery(const GfxRenderer& r, const int x, const int y, const int percent, const bool showNumber) {
  const int body[5][2] = {{x + 1, y - 9}, {x + 33, y - 10}, {x + 34, y + 8}, {x, y + 9}, {x + 1, y - 9}};
  polyline(r, body, 5, 2);
  stroke(r, x + 35, y - 3, x + 39, y - 2, 2);
  stroke(r, x + 39, y - 2, x + 39, y + 3, 2);
  stroke(r, x + 39, y + 3, x + 35, y + 3, 2);
  const int level = std::clamp(percent, 0, 100);
  if (showNumber) return batteryDigits(r, x, y, level);
  for (int px = x + 4; px < x + 4 + 26 * level / 100; px += 3) stroke(r, px, y - 6, px + 1, y + 6, 1);
}

namespace {
// 1/16 px to px, rounded away from zero.
int sixteenths(const int v) { return (v + (v < 0 ? -8 : 8)) / 16; }

// The header clock as clockShowInHeader asks: the time, or the time and the day/month (after it, or into
// `date` when one is given). False when it is hidden or the clock was never set.
bool clockText(char* out, const size_t size, char* date = nullptr, const size_t dateSize = 0) {
  if (date) *date = 0;
  const uint8_t mode = SETTINGS.clockShowInHeader;
  if (mode == CrossPointSettings::CLOCK_HEADER_HIDE || !clockstatus::hasValidTime() ||
      !halClock.formatTime(out, size, SETTINGS.clockFormat == 1))
    return false;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0;
  if (mode == CrossPointSettings::CLOCK_HEADER_TIME_DATE && halClock.getDateTime(year, month, day, hour, minute)) {
    const size_t used = date ? 0 : strlen(out);
    snprintf(date ? date : out + used, date ? dateSize : size - used, date ? "%u/%u" : " %u/%u", static_cast<unsigned>(day),
             static_cast<unsigned>(month));
  }
  return true;
}
}  // namespace

void mark(const GfxRenderer& r, const Mark m, const int cx, const int cy) {
  if (m == Mark::Tick) {  // every tick of the shell is the one hand-drawn tick, some 20 px across
    penTick(r, cx - 2, cy + 6, 20, 2);
    return;
  }
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
  const int level = std::clamp(static_cast<int>(powerManager.getDisplayedBatteryPercentage()), 0, 100);
  battery(r, 14, y, level, SETTINGS.batteryPercentShown(false));
  // The date has no room beside the clock (the Down mark): it sits in the gap between the Confirm and Up marks.
  char clock[12], date[8];
  if (clockText(clock, sizeof(clock), date, sizeof(date))) {
    text(r, Size::S22, w - 14 - width(r, Size::S22, clock), y + 8, clock);
    if (*date) text(r, Size::S22, (w >= 528 ? 264 : 240) - width(r, Size::S22, date) / 2, y + 8, date);
  }
  // Front Left and Right walk up and down the screen, so their marks point up and down.
  const auto labels = input.mapLabels(hints.back ? "b" : "", hints.confirm ? "c" : "", hints.left ? "u" : "", hints.right ? "d" : "");
  static constexpr int WIDE[4] = {105, 197, 331, 423}, NARROW[4] = {98, 186, 294, 382};
  const int* centres = w >= 528 ? WIDE : NARROW;
  const char* marks[4] = {labels.btn1, labels.btn2, labels.btn3, labels.btn4};
  for (int i = 0; i < 4; ++i)
    if (marks[i] && marks[i][0]) {
      mark(r, marks[i][0] == 'b' ? Mark::Back : marks[i][0] == 'c' ? Mark::Tick : marks[i][0] == 'u' ? Mark::Up : Mark::Down, centres[i], y);
    }
}

#if FREEINK_DEVICE_X4PRO
static_assert(touch::CLOCK_HIDE == CrossPointSettings::CLOCK_HEADER_HIDE && touch::CLOCK_TIME == CrossPointSettings::CLOCK_HEADER_TIME &&
                  touch::CLOCK_TIME_DATE == CrossPointSettings::CLOCK_HEADER_TIME_DATE,
              "the band's clock values are clockShowInHeader's");

void topBar(const GfxRenderer& r, const char* left) {
  // The battery stays at x 412, shown or struck off: a ring finds it where it was.
  const int level = std::clamp(static_cast<int>(powerManager.getDisplayedBatteryPercentage()), 0, 100);
  char pct[6] = "";
  if (!SETTINGS.uglyBatteryHidden && SETTINGS.batteryPercentShown(false)) snprintf(pct, sizeof(pct), "%d%%", level);
  const int clockRight = 400 - (*pct ? width(r, Size::S22, pct) + 12 : 0);
  char clock[20];
  const bool hasClock = clockText(clock, sizeof(clock));
  const int clockX = hasClock ? clockRight - width(r, Size::S22, clock) : clockRight;
  if (hasClock) text(r, Size::S22, clockX, 34, clock);
  if (left && *left) text(r, Size::S22, 24, 34, fit(r, Size::S22, left, std::min(300, clockX - 40)).c_str());
  if (SETTINGS.uglyBatteryHidden) return;
  constexpr int X0 = 412, X1 = 456, Y0 = 12, Y1 = 37;
  const int body[5][2] = {{X0, Y0 + 1}, {X1, Y0}, {X1 + 1, Y1}, {X0 - 1, Y1 + 1}, {X0, Y0 + 1}};
  polyline(r, body, 5, 2);
  stroke(r, X1 + 2, Y0 + 8, X1 + 6, Y0 + 8, 2);
  stroke(r, X1 + 6, Y0 + 8, X1 + 6, Y1 - 8, 2);
  stroke(r, X1 + 6, Y1 - 8, X1 + 2, Y1 - 8, 2);
  for (int px = X0 + 3; px < X0 + 3 + (X1 - X0 - 6) * level / 100; px += 3) stroke(r, px, Y0 + 4, px + 1, Y1 - 4, 1);
  if (*pct) text(r, Size::S22, 400 - width(r, Size::S22, pct), 34, pct);
}

void formTopBar(const GfxRenderer& r) {
  if (SETTINGS.globalStatusBarHidden()) return;
  char clock[20];
  if (clockText(clock, sizeof(clock))) text(r, Size::S22, 24, 34, clock);
  if (SETTINGS.uglyBatteryHidden) return;
  const int x0 = r.getScreenWidth() - 68, x1 = r.getScreenWidth() - 24;
  constexpr int y0 = 12, y1 = 37;
  const int body[5][2] = {{x0, y0 + 1}, {x1, y0}, {x1 + 1, y1}, {x0 - 1, y1 + 1}, {x0, y0 + 1}};
  polyline(r, body, 5, 2);
  stroke(r, x1 + 2, y0 + 8, x1 + 6, y0 + 8, 2);
  stroke(r, x1 + 6, y0 + 8, x1 + 6, y1 - 8, 2);
  stroke(r, x1 + 6, y1 - 8, x1 + 2, y1 - 8, 2);
  const int level = std::clamp(static_cast<int>(powerManager.getDisplayedBatteryPercentage()), 0, 100);
  for (int x = x0 + 3; x < x0 + 3 + (x1 - x0 - 6) * level / 100; x += 3) stroke(r, x, y0 + 4, x + 1, y1 - 4, 1);
  if (SETTINGS.batteryPercentShown(false)) {
    char pct[6];
    snprintf(pct, sizeof(pct), "%d%%", level);
    text(r, Size::S22, x0 - 12 - width(r, Size::S22, pct), 34, pct);
  }
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

void paper(const GfxRenderer& r, const int top, const int bottom, const bool tornTop, const bool tornBottom, const uint32_t seed,
           const int listShift) {
  const int e0 = touch::rubEdge(top - 12, true, listShift), e1 = touch::rubEdge(bottom + 12, false, listShift);
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
  if (ticked) penTick(r, x - 12, y + 7, 26, 3);  // the one tick of the shell
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

void heart(const GfxRenderer& r, const int x, const int y, const int size) {
  constexpr int path[11][2] = {{0, 11}, {-9, 2}, {-11, -4}, {-8, -9}, {-3, -9}, {0, -4},
                               {3, -9}, {8, -10}, {12, -4}, {9, 3}, {1, 11}};
  for (int i = 0; i < 10; ++i)
    stroke(r, x + path[i][0] * size / 24, y + path[i][1] * size / 24,
           x + path[i + 1][0] * size / 24, y + path[i + 1][1] * size / 24, 2);
}

}  // namespace ugly
