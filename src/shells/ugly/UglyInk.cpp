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
#include "components/TenorMenuChrome.h"
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
  return cp == MARGIN_PIN || cp == SCRAWL || cp == TICK || (cp >= KEY_FIRST && cp <= KEY_LAST) ||
         (cp >= 0x2190 && cp <= 0x2193) || cp == 0x2665 || cp == 0x2764 || cp == 0x2605;
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
  if (penDrawn(cp)) return 28 + 2 * stepOf(r, s, false, pos, ' ', {nullptr, logic::NO_WARP});
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
  icon(r, icons::Id::Star, {cx - 12, cy - 12, 24, 24}, iconFrameSeed());
}

// The erase key, a tag with a cross in it, of one stroke and one more for the cross, some 20 px across.
void penErase(const GfxRenderer& r, const int cx, const int cy) {
  icon(r, icons::Id::Erase, {cx - 12, cy - 12, 24, 24}, iconFrameSeed());
}

// One key symbol of the UI strings, centred on (cx, cy). The star, the side buttons and the erase key are the keys'
// own, as buttonSymbols resolves them (the side buttons follow the reading side layout).
void keyMark(const GfxRenderer& r, const uint32_t cp, const int cx, const int cy) {
  static constexpr Mark KEYS[6] = {Mark::Tick, Mark::Back, Mark::Up, Mark::Down, Mark::Left, Mark::Right};
  if (cp == TICK) return mark(r, Mark::Tick, cx, cy);
  if (cp >= 0x2190 && cp <= 0x2193) {
    constexpr Mark ARROWS[] = {Mark::Left, Mark::Up, Mark::Right, Mark::Down};
    return mark(r, ARROWS[cp - 0x2190], cx, cy);
  }
  if (cp == 0x2665 || cp == 0x2764) return heart(r, cx, cy, 24);
  if (cp == 0x2605) return penStar(r, cx, cy);
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
        const bool draw, const bool straight, Box* ink = nullptr) {
  const int fid = idOf(s), fb = fallbackFont(s);
  const bool af = !straight && wild();
  const int px = pixelsOf(s);
  const int top = baseline - r.getFontAscenderSize(fid);
  int cursor = x, pos = 0;
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  char one[5];
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    const Letter letter = letterOf(s, af, cp);
    if (ink && cp != ' ' && !penDrawn(cp) && !penDegree(cp)) {
      const auto include = [&](const int gx, const int gy, const int gw, const int gh) {
        if (gw <= 0 || gh <= 0) return;
        ink->x0 = std::min(ink->x0, cursor + gx);
        ink->y0 = std::min(ink->y0, baseline + gy);
        ink->x1 = std::max(ink->x1, cursor + gx + gw);
        ink->y1 = std::max(ink->y1, baseline + gy + gh);
      };
      if (letter.glyph) {
        const auto* g = letter.glyph;
        logic::warpGlyph(FONTS[static_cast<int>(s)]->data->bitmap + g->dataOffset, g->width, g->height,
                         g->left, g->top, g->advanceX, letter.warp, logic::NO_CLIP,
                         [&](int px, int py) { include(px, py + logic::jumpDy(pos, cp, pixelsOf(s)), 1, 1); });
      } else {
        const auto family = r.getFontMap().find(baked(s, cp) ? fid : fb);
        const auto* g = family == r.getFontMap().end() ? nullptr : family->second.getGlyph(cp, EpdFontFamily::REGULAR);
        if (g) include(g->left, -g->top, g->width, g->height);
      }
    }
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

void penStroke(const GfxRenderer& r, int x0, int y0, int x1, int y1, int width) {
  stroke(r, x0, y0, x1, y1, width);
}

void ensureFonts(GfxRenderer& r) {
  if (r.getFontMap().count(FONT_IDS[0])) return;
  r.insertFont(FONT_IDS[0], EpdFontFamily(&FONT22));
  r.insertFont(FONT_IDS[1], EpdFontFamily(&FONT30));
  r.insertFont(FONT_IDS[2], EpdFontFamily(&FONT38));
  r.insertFont(FONT_IDS[3], EpdFontFamily(&FONT52));
}

// Read the already drawn label before its selection circle is added, in any panel orientation.
Box inkBounds(const GfxRenderer& r, Box area) {
  area.x0 = std::max(0, area.x0); area.y0 = std::max(0, area.y0);
  area.x1 = std::min(r.getScreenWidth(), area.x1); area.y1 = std::min(r.getScreenHeight(), area.y1);
  Box ink{area.x1, area.y1, area.x0, area.y0};
  DirectPixelWriter pen;
  pen.init(const_cast<GfxRenderer&>(r));
  const int stride = pen.displayWidthBytes * 8;
  for (int y = area.y0; y < area.y1; ++y) for (int x = area.x0; x < area.x1; ++x) {
    const int py = pen.phyYBase + x * pen.phyYStepX + y * pen.phyYStepY - pen.originY;
    if (py < 0 || py >= pen.clipRows) continue;
    const int bit = py * stride + pen.phyXBase + x * pen.phyXStepX + y * pen.phyXStepY;
    if (pen.fb[bit >> 3] & (0x80 >> (bit & 7))) continue;
    ink.x0 = std::min(ink.x0, x); ink.y0 = std::min(ink.y0, y);
    ink.x1 = std::max(ink.x1, x + 1); ink.y1 = std::max(ink.y1, y + 1);
  }
  return ink.x0 < ink.x1 && ink.y0 < ink.y1 ? ink : area;
}

int text(const GfxRenderer& r, const Size s, const int x, const int baseline, const char* utf8, const bool black,
         const bool straight) {
  return run(r, s, x, baseline, utf8ComposeNfc(utf8), black, true, straight);
}

int width(const GfxRenderer& r, const Size s, const char* utf8, const bool straight) {
  return run(r, s, 0, 0, utf8ComposeNfc(utf8), true, false, straight);
}

int ascent(const Size s) { return ASCENT[static_cast<int>(s)]; }

Box textBounds(const GfxRenderer& r, const Size s, const char* utf8) {
  Box ink{4096, 4096, -4096, -4096};
  run(r, s, 0, 0, utf8ComposeNfc(utf8), true, false, false, &ink);
  return ink.x1 > ink.x0 ? ink : Box{};
}

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

void circle(const GfxRenderer& r, const Circle role, const Box& box, const int padX, const int padY, const uint32_t seed,
            const int w) {
  const CirclePoint* pts = role == Circle::Word ? CIRCLE_WORD : role == Circle::Object ? CIRCLE_OBJECT : CIRCLE_ROW;
  if (w <= 0 || box.x0 >= box.x1 || box.y0 >= box.y1) return;
  const int margin = w / 2;
  const bool padded = padX > 0 && padY > 0;
  const int safeX = padded ? std::max(padX, 34) : padX;
  const int safeY = padded ? std::max(padY, 19) : padY;
  const int left = std::max(margin, box.x0 - safeX + margin);
  const int right = std::min(r.getScreenWidth() - 1 - margin, box.x1 + safeX - margin);
  const int top = std::max(margin, box.y0 - safeY + margin);
  const int bottom = std::min(r.getScreenHeight() - 1 - margin, box.y1 + safeY - margin);
  if (left >= right || top >= bottom) return;
  const int cx = padded ? (box.x0 + box.x1) / 2 : (left + right) / 2;
  const int cy = padded ? (box.y0 + box.y1) / 2 : (top + bottom) / 2;
  int rx = padded ? (box.x1 - box.x0) / 2 + std::max(padX, 28) + logic::wobble(seed, 0, 3) : (right - left) / 2;
  int ry = padded ? (box.y1 - box.y0) / 2 + std::max(padY, 16) + logic::wobble(seed, 1, 1) : (bottom - top) / 2;
  const int tilt = -4 + logic::wobble(seed, 2, box.x1 - box.x0 < 200 ? 1 : 2);
  const int shiftX = logic::wobble(seed, 4, 2), shiftY = logic::wobble(seed, 5, 1);
  constexpr int SINE[] = {0, 98, 181, 237, 256, 237, 181, 98, 0, -98, -181, -237, -256, -237, -181, -98};
  const int phase = (logic::wobble(seed, 6, 7) + 16) & 15;
  const int turn = logic::wobble(seed, 6, 2);
  const int sine = padded ? turn * 16 : SINE[phase];
  const int cosine = padded ? (std::abs(turn) == 2 ? 254 : turn ? 255 : 256) : SINE[(phase + 4) & 15];
  const int count =
      (role == Circle::Word ? CIRCLE_WORD_COUNT : role == Circle::Object ? CIRCLE_OBJECT_COUNT : CIRCLE_ROW_COUNT) -
      (logic::wobble(seed, 3, 2) + 2);
  for (int index = 0; index < count; ++index) {
    const int ux = (pts[index].x * cosine - pts[index].y * sine) / 256;
    if (ux < 0) rx = std::min(rx, std::max(1, (cx + shiftX - left) * 256 / -ux));
    if (ux > 0) rx = std::min(rx, std::max(1, (right - cx - shiftX) * 256 / ux));
  }
  for (int index = 0; index < count; ++index) {
    const int ux = (pts[index].x * cosine - pts[index].y * sine) / 256;
    const int uy = (pts[index].x * sine + pts[index].y * cosine) / 256;
    const int centerY = cy + rx * ux / 256 * tilt / 100 + shiftY;
    if (uy < 0) ry = std::min(ry, std::max(1, (centerY - top) * 256 / -uy));
    if (uy > 0) ry = std::min(ry, std::max(1, (bottom - centerY) * 256 / uy));
  }
  int px = 0, py = 0;
  for (int index = 0; index < count; ++index) {
    const int ux = (pts[index].x * cosine - pts[index].y * sine) / 256;
    const int uy = (pts[index].x * sine + pts[index].y * cosine) / 256;
    const int offsetX = rx * ux / 256;
    const int x = std::clamp(cx + offsetX + shiftX, left, right);
    const int y = std::clamp(cy + ry * uy / 256 + offsetX * tilt / 100 + shiftY, top, bottom);
    if (index > 0) stroke(r, px, py, x, y, w);
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
  (void)width;
  icon(r, icons::Id::Tick, {x - span / 2, y - span, span, span}, iconFrameSeed());
}
}  // namespace

void tick(const GfxRenderer& r, const int x, const int y) { penTick(r, x, y, 32, 3); }

void battery(const GfxRenderer& r, const int x, const int y, const int percent, const bool showNumber) {
  icon(r, icons::Id::Battery, {x, y - 14, 42, 28}, iconFrameSeed());
  if (showNumber) return;  // the body stays empty: the level is written beside it (statusBar)
  const int level = std::clamp(percent, 0, 100);
  for (int px = x + 4; px < x + 4 + 26 * level / 100; px += 3) stroke(r, px, y - 6, px + 1, y + 6, 1);
}

namespace {
// The header clock as clockShowInHeader asks: the time, or the time and the day/month (after it, or into
// `date` when one is given). False when it is hidden or the clock was never set.
bool clockText(char* out, const size_t size, char* date = nullptr, const size_t dateSize = 0, const bool footer = false) {
  if (date) *date = 0;
  const uint8_t mode = SETTINGS.clockShowInHeader;
  if ((!footer && mode == CrossPointSettings::CLOCK_HEADER_HIDE) || !clockstatus::hasValidTime() ||
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
  constexpr icons::Id IDS[] = {icons::Id::Left, icons::Id::Right, icons::Id::Up, icons::Id::Down,
                              icons::Id::Tick, icons::Id::Back};
  icon(r, IDS[static_cast<unsigned>(m)], {cx - 12, cy - 12, 24, 24}, iconFrameSeed());
}

void pageHints(const GfxRenderer& r, const char* before, const char* after, int baseline) {
  if (tenorchrome::x3ScreenLayoutEnabled()) {
    const auto bar = tenorchrome::x3ScreenLayout(r).barRect;
    r.fillRect(bar.x, bar.y, bar.width, bar.height, false);
    baseline = bar.y + (bar.height + ascent(Size::S22)) / 2;
  }
  const int w = r.getScreenWidth();
  mark(r, Mark::Left, 32, baseline - 7);
  text(r, Size::S22, 52, baseline, before);
  mark(r, Mark::Right, w - 32, baseline - 7);
  text(r, Size::S22, w - 52 - width(r, Size::S22, after), baseline, after);
}

int pathInkTop(const GfxRenderer& r) {
  const auto bar = tenorchrome::x3ScreenLayout(r).barRect;
  return bar.y + (bar.height - ascent(Size::S22)) / 2;
}

void pathHints(const GfxRenderer& r, const char* path, const char* note, const bool wholeBar) {
  if (!tenorchrome::x3ScreenLayoutEnabled()) return;
  const auto bar = tenorchrome::x3ScreenLayout(r).barRect;
  const int baseline = bar.y + (bar.height + ascent(Size::S22)) / 2;
  const int wipeTop = wholeBar ? bar.y : std::max(bar.y, baseline - ascent(Size::S22) - 8);
  r.fillRect(bar.x, wipeTop, bar.width, bar.y + bar.height - wipeTop, false);
  const auto info = fit(r, Size::S22, note ? note : "", bar.width - 48);
  const int reserve = info.empty() ? 0 : width(r, Size::S22, info.c_str()) + 16;
  const int room = std::max(0, bar.width - 48 - reserve);
  const char* tail = path ? path : "";
  std::string shown = tail;
  if (width(r, Size::S22, tail) > room) {
    const int available = room - width(r, Size::S22, "\xEE\x80\x80");
    while (*tail && width(r, Size::S22, tail) > available) {
      ++tail;
      while (*tail && (static_cast<unsigned char>(*tail) & 0xC0) == 0x80) ++tail;
    }
    shown = available >= 0 ? std::string("\xEE\x80\x80") + tail : "";
  }
  text(r, Size::S22, 24, baseline, shown.c_str());
  if (!info.empty()) text(r, Size::S22, bar.width - 24 - width(r, Size::S22, info.c_str()), baseline, info.c_str());
}

void statusBar(const GfxRenderer& r, const char* btn1, const char* btn2, const char* btn3, const char* btn4) {
  if (SETTINGS.globalStatusBarHidden()) return;
  if (!tenorchrome::buttonHintsShown()) btn1 = btn2 = btn3 = btn4 = "";
  const int w = r.getScreenWidth(), h = r.getScreenHeight();
  const int y = h - 20;
  const int level = std::clamp(static_cast<int>(powerManager.getDisplayedBatteryPercentage()), 0, 100);
  const bool clockLeft = SETTINGS.statusBarClock == CrossPointSettings::STATUS_BAR_CLOCK_LEFT;
  // The level in the pen beside the battery, as readable as the clock: on the screen's side of the clock
  // when the battery is on the right, to its right otherwise. The marks make room for it below.
  const bool digits = SETTINGS.batteryPercentShown(false);
  char level_text[6] = "";
  if (digits) snprintf(level_text, sizeof(level_text), "%d%%", level);
  const int levelWidth = digits ? width(r, Size::S22, level_text) : 0;
  const int levelRoom = levelWidth;
  const int batteryX = clockLeft ? w - 56 : 14;
  battery(r, batteryX, y, level, digits);
  if (digits) text(r, Size::S22, clockLeft ? batteryX - 1 - levelWidth : batteryX + 45, y + 8, level_text);
  char clock[12] = "", date[8];
  const bool hasClock = clockText(clock, sizeof(clock), date, sizeof(date), true);
  const int clockWidth = hasClock ? width(r, Size::S22, clock) : 0;
  if (hasClock) text(r, Size::S22, clockLeft ? 14 : w - 14 - clockWidth, y + 8, clock);
  // Front Left and Right walk up and down the screen, so their marks point up and down.
  static constexpr Mark MARKS[6] = {Mark::Tick, Mark::Back, Mark::Up, Mark::Down, Mark::Left, Mark::Right};
  static constexpr int WIDE[5] = {105, 197, 264, 331, 423}, NARROW[5] = {98, 186, 240, 294, 382};
  const int* centres = w >= 528 ? WIDE : NARROW;
  const char* words[5] = {btn1, btn2, hasClock ? date : "", btn3, btn4};
  const int leftEnd = clockLeft ? 18 + clockWidth : 60 + levelRoom;
  const int rightEnd = clockLeft ? w - 60 - levelRoom : w - 18 - clockWidth;
  int positions[5], spans[5] = {};
  std::string fitted[5];
  int leftEdge = leftEnd + 6;
  for (int index = 0; index < 5; ++index) {
    if (!words[index] || !*words[index]) continue;
    const int id = buttonSymbols::labelId(words[index]);
    if (id >= 0) {
      spans[index] = 28;
    } else {
      fitted[index] = index == 2 ? words[index]
                                : fit(r, Size::S22, words[index], std::min(84, (rightEnd - leftEnd - 24) / 5));
      spans[index] = width(r, Size::S22, fitted[index].c_str()) + 8;
    }
    positions[index] = std::max(centres[index] - spans[index] / 2, leftEdge);
    leftEdge = positions[index] + spans[index] + 6;
  }
  int rightEdge = rightEnd - 6;
  for (int index = 4; index >= 0; --index) {
    if (!spans[index]) continue;
    positions[index] = std::min(positions[index], rightEdge - spans[index]);
    rightEdge = positions[index] - 6;
  }
  for (int index = 0; index < 5; ++index) {
    if (!words[index] || !*words[index]) continue;
    const int id = buttonSymbols::labelId(words[index]);
    if (id >= 0) {
      mark(r, MARKS[id], positions[index] + spans[index] / 2, y);
    } else {
      text(r, Size::S22, positions[index] + 4, y + 8, fitted[index].c_str());
    }
  }
}

void statusBar(const GfxRenderer& r, const MappedInputManager& input, const Hints hints) {
  const auto labels = input.mapLabels(hints.back ? "\xEE\x84\x81" : "", hints.confirm ? "\xEE\x84\x80" : "",
                                      hints.left ? "\xEE\x84\x82" : "", hints.right ? "\xEE\x84\x83" : "");
  statusBar(r, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
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
  icon(r, icons::Id::Battery, {X0 - 3, Y0 - 4, X1 - X0 + 13, Y1 - Y0 + 9}, iconFrameSeed());
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
  icon(r, icons::Id::Battery, {x0 - 3, y0 - 4, x1 - x0 + 13, y1 - y0 + 9}, iconFrameSeed());
  const int level = std::clamp(static_cast<int>(powerManager.getDisplayedBatteryPercentage()), 0, 100);
  for (int x = x0 + 3; x < x0 + 3 + (x1 - x0 - 6) * level / 100; x += 3) stroke(r, x, y0 + 4, x + 1, y1 - 4, 1);
  if (SETTINGS.batteryPercentShown(false)) {
    char pct[6];
    snprintf(pct, sizeof(pct), "%d%%", level);
    text(r, Size::S22, x0 - 12 - width(r, Size::S22, pct), 34, pct);
  }
}

void arrow(const GfxRenderer& r, const int x, const int y, const bool down, const int length) {
  icon(r, down ? icons::Id::Down : icons::Id::Up, {x - 12, y - length / 2 - 3, 24, length + 6}, iconFrameSeed());
}

void navRow(const GfxRenderer& r, const char* prev, const char* back, const char* next) {
  constexpr int BASE = 758;
  if (prev && *prev) {
    mark(r, Mark::Left, 26, BASE - 10);
    text(r, Size::S30, 46, BASE, prev);
  }
  if (back && *back) {
    text(r, Size::S22, 240 - width(r, Size::S22, back) / 2, BASE - 6, back);
    arrow(r, 240, BASE + 12, true, 18);
  }
  if (next && *next) {
    mark(r, Mark::Right, 454, BASE - 10);
    text(r, Size::S30, 434 - width(r, Size::S30, next), BASE, next);
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
  icon(r, icons::Id::Heart, {x - size / 2, y - size / 2, size, size}, iconFrameSeed());
}

}  // namespace ugly
