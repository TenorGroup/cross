// Renders the production Back-arrow and Select-tick shapes (both now the
// fixed 1-bit bitmaps, see InlineSymbolBitmaps.h) into a real
// GfxRenderer framebuffer and checks two things end to end for each:
//   1. ButtonSymbols::horizontalBounds still reports the true ink extent, so
//      the footer's cell-clearing and corner clamping stay correct.
//   2. The new shape's pixels differ from the glyph it replaced.
// Follows the sd_font_lifetime recipe: HalDisplay's frame buffer is a plain
// RAM vector, so the real GfxRenderer paints into memory this test can read.
#include <ButtonSymbols.h>
#include "BackV1056Reference.h"
#include "EnterReference.h"
#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <InlineSymbols.h>
#include <InlineSymbolBitmaps.h>
#include <TenorMenuChrome.h>
#include <fontIds.h>
#include <builtinFonts/geist_8_regular.h>
#include <builtinFonts/geist_10_regular.h>
#include <builtinFonts/geist_12_regular.h>
#include <builtinFonts/geist_14_regular.h>
#include <builtinFonts/geist_16_regular.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE, 0xff);
void clear() { std::fill(pixels.begin(), pixels.end(), 0xff); }

// bit == 0 means ink: GfxRenderer::drawPixel clears the bit for state=true,
// and clearScreen/the initial fill leave 0xff ("all white") behind.
bool isInk(int x, int y) {
  if (x < 0 || y < 0 || x >= HalDisplay::DISPLAY_WIDTH || y >= HalDisplay::DISPLAY_HEIGHT) return false;
  const uint8_t byte = pixels[static_cast<size_t>(y) * HalDisplay::DISPLAY_WIDTH_BYTES + x / 8];
  return ((byte >> (7 - (x % 8))) & 1) == 0;
}

struct Bounds {
  int minX, maxX, minY, maxY;
  bool any;
};

// Scans a window around (cx, cy) for ink and returns its extent. The window
// is generous relative to the icon sizes under test (net 14 and 18).
Bounds scanAround(int cx, int cy, int radius) {
  Bounds b{0, 0, 0, 0, false};
  for (int y = cy - radius; y <= cy + radius; ++y) {
    for (int x = cx - radius; x <= cx + radius; ++x) {
      if (!isInk(x, y)) continue;
      if (!b.any) {
        b.minX = b.maxX = x;
        b.minY = b.maxY = y;
        b.any = true;
      } else {
        b.minX = std::min(b.minX, x);
        b.maxX = std::max(b.maxX, x);
        b.minY = std::min(b.minY, y);
        b.maxY = std::max(b.maxY, y);
      }
    }
  }
  return b;
}

// Copies the scan window into a flat buffer so two renders can be compared
// pixel-for-pixel regardless of where their ink bounds land.
std::vector<uint8_t> snapshot(int cx, int cy, int radius) {
  std::vector<uint8_t> out(static_cast<size_t>(2 * radius + 1) * (2 * radius + 1));
  size_t i = 0;
  for (int y = cy - radius; y <= cy + radius; ++y)
    for (int x = cx - radius; x <= cx + radius; ++x) out[i++] = isInk(x, y) ? 1 : 0;
  return out;
}

// Reference oracle: the shape Back drew before this task, two small filled
// triangles pointing left, side by side (see lib/GfxRenderer/InlineSymbols.cpp
// history). Reproduced here, not in production code, purely so this test can
// prove the new render is a different glyph.
void drawOldBackShape(const GfxRenderer& r, int x, int y, int size, bool black) {
  const int h = std::max(3, size / 2);
  const int half = std::max(2, h / 2);
  for (int i = 0; i < 2; ++i)
    for (int col = 0; col <= half * 2; ++col) {
      const int cx = x + (i == 0 ? -half - 1 : half + 1);
      const int dy = col * h / (half * 2);
      const int px = cx + (-1) * (half - col);
      r.drawLine(px, y - dy, px, y + dy, black);
    }
}

// Reference oracle: the checkmark Select drew before this task, a filled
// polygon (see lib/GfxRenderer/InlineSymbols.cpp history), reproduced here so
// this test can prove the new bitmap is a different glyph.
void drawOldSelectShape(const GfxRenderer& r, int x, int y, int size, bool black) {
  const int h = std::max(3, size / 2);
  const int xs[] = {x - h, x - h * 2 / 3, x - h / 4, x + h * 3 / 4, x + h, x - h / 4};
  const int ys[] = {y, y - h / 3, y + h / 6, y - h, y - h * 2 / 3, y + h * 3 / 4};
  r.fillPolygon(xs, ys, 6, black);
}
}  // namespace

// ButtonSymbols.cpp reads this to decide whether the device has a touch
// panel; the symbol-drawing footer path is off, so it must report false.
HalGPIO gpio;

// The display boundary supplies a RAM framebuffer; the real GfxRenderer
// renders into it, matching the sd_font_lifetime host-test setup.
HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

int main(int argc, char** argv) {
  buttonSymbols::install();
  HalDisplay display;
  GfxRenderer renderer{display};
  renderer.begin();
  // Identity coordinate mapping (phyX=x, phyY=y), so ink bounds read directly
  // in the same x the shape functions and horizontalBounds() use.
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);

  constexpr int kCx = 200, kCy = 200;
  constexpr int kRadius = 40;

  if (argc == 2 && std::strcmp(argv[1], "--enter-reference") == 0) {
    for (const int net : {14, 18}) {
      const auto& expected = net == 14 ? enterReference::net14 : enterReference::net18;
      const auto& actual = net == 14 ? inlineSymbolBitmaps::kSelectNet14 : inlineSymbolBitmaps::kSelectNet18;
      const auto& back = net == 14 ? backV1056::kBackNet14 : backV1056::kBackNet18;
      check(actual.width == expected.width && actual.height == expected.height, "Select must use Enter dimensions");
      check(actual.height == back.height, "Select and Back must have equal height");
      const auto ink = [](const inlineSymbolBitmaps::Glyph& g, int x, int y) {
        return x >= 0 && y >= 0 && x < g.width && y < g.height && (g.rows[y] & (1u << (g.width - 1 - x)));
      };
      // Stroke measured on Back: shaft thickness mid-way, and the width of its right-hand vertical.
      int stroke = 0, backVertical = 0;
      for (int row = 0; row < back.height; ++row) stroke += ink(back, 12, row) ? 1 : 0;
      for (int col = net == 14 ? 18 : 20; col < back.width; ++col)
        backVertical += ink(back, col, net == 14 ? 9 : 12) ? 1 : 0;
      check(stroke == backVertical, "Back shaft and vertical differ in thickness");
      // Horizontal shaft: same thickness as Back at every column between head and bend.
      const int shaftLo = net == 14 ? 8 : 10, shaftHi = net == 14 ? 15 : 20;
      for (int col = shaftLo; col <= shaftHi; ++col) {
        int run = 0;
        for (int row = 0; row < actual.height; ++row) run += ink(actual, col, row) ? 1 : 0;
        check(run == stroke, "Enter shaft thickness differs from the Back stroke");
      }
      // Vertical stem: same thickness as Back on every row between the cap and the bend, round cap on top.
      const int stemX = net == 14 ? 19 : 25, bendTop = net == 14 ? 5 : 6;
      for (int row = 1; row < bendTop; ++row) {
        int run = 0;
        for (int col = 0; col < actual.width; ++col) run += (col >= stemX && ink(actual, col, row)) ? 1 : 0;
        check(run == stroke, "Enter stem thickness differs from the Back stroke");
      }
      {
        int cap = 0;
        for (int col = 0; col < actual.width; ++col) cap += ink(actual, col, 0) ? 1 : 0;
        check(cap == stroke - 2 && ink(actual, stemX + 1, 0) && ink(actual, stemX + stroke - 2, 0),
              "Enter top cap must be the round cap of the stroke");
      }
      // Arrowhead: the Back head, mirrored about the shaft axis.
      const int headWidth = net == 14 ? 8 : 10, axis = net == 14 ? 8 : 10;
      // Rows above the shaft come straight from Back (shifted down by 2); Back's own lower wing at 14 is not symmetric,
      // so there only the upper wing is compared and the lower one is checked as its mirror image below.
      for (int row = 0; row < (net == 14 ? 8 : back.height - 2); ++row)
        for (int x = 0; x < headWidth; ++x)
          check(bool(ink(actual, x, row + 2)) == bool(ink(back, x, row)),
                "Enter arrowhead must preserve the v1.0.56 Back pixels");
      // At 18 the shaft is 4 px so its bottom row is shared with the lower wing; compare wings by their free part.
      for (int k = 2; k <= axis - 2; ++k)
        for (int x = 0; x < (k == 2 && net == 18 ? 7 : headWidth); ++x)
          check(ink(actual, x, axis - k) == ink(actual, x, axis + k), "Enter arrowhead is not symmetric about the shaft");
      // Bend: quarter ring symmetric about its diagonal, run in every row and column within 0.6 px of the ideal ring.
      const int ring = net == 14 ? 5 : 7, bx = actual.width - ring, by = net == 14 ? 5 : 6;
      double worst = 0;
      for (int i = 0; i < ring; ++i) {
        const double y = i + 0.5, outer = std::sqrt(ring * ring - y * y);
        const double inner = std::sqrt(std::max(0.0, double((ring - stroke) * (ring - stroke)) - y * y));
        int rowRun = 0, colRun = 0;
        for (int j = 0; j < ring; ++j) {
          // Block mirrored so that the circle centre is its top-left corner and the stem sits on the right.
          rowRun += ink(actual, bx + j, by + i) ? 1 : 0;
          colRun += ink(actual, bx + i, by + j) ? 1 : 0;
          check(ink(actual, bx + j, by + i) == ink(actual, bx + i, by + j), "Enter bend is not diagonal-symmetric");
        }
        worst = std::max({worst, std::fabs(rowRun - (outer - inner)), std::fabs(colRun - (outer - inner))});
      }
      check(worst <= 0.6, "Enter bend swells or pinches the stroke");
      // No stray pixel: every ink pixel touches another, and the glyph is a single piece.
      int inkCount = 0, reached = 0;
      std::vector<std::pair<int, int>> pending;
      std::vector<bool> seen(actual.width * actual.height, false);
      for (int y = 0; y < actual.height; ++y)
        for (int x = 0; x < actual.width; ++x)
          if (ink(actual, x, y)) {
            ++inkCount;
            if (pending.empty() && reached == 0) { pending.push_back({x, y}); seen[y * actual.width + x] = true; }
            check(ink(actual, x - 1, y) || ink(actual, x + 1, y) || ink(actual, x, y - 1) || ink(actual, x, y + 1),
                  "Enter has an isolated pixel");
          }
      while (!pending.empty()) {
        const auto [x, y] = pending.back();
        pending.pop_back();
        ++reached;
        for (const auto [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}})
          if (ink(actual, x + dx, y + dy) && !seen[(y + dy) * actual.width + x + dx]) {
            seen[(y + dy) * actual.width + x + dx] = true;
            pending.push_back({x + dx, y + dy});
          }
      }
      check(reached == inkCount, "Enter is not one connected piece");
      printf("Enter net=%d stroke=%d (Back=%d) bend_worst_error=%.2f ink=%d\n", net, stroke, stroke, worst, inkCount);
      for (int path = 0; path < 2; ++path) {
        clear();
        if (path == 0) buttonSymbols::drawLabel(renderer, "\xEE\x84\x80", kCx, kCy, net);
        else inlineSymbols::drawShape(renderer, inlineSymbols::Shape::Select, kCx, kCy, net, true);
        const auto bounds = scanAround(kCx, kCy, kRadius);
        check(bounds.any && std::abs(bounds.minY + bounds.maxY - 2 * kCy) <= 1, "Enter center exceeds 0.5 pixel");
        for (int dy = -kRadius; dy <= kRadius; ++dy)
          for (int dx = -kRadius; dx <= kRadius; ++dx) {
            const int row = dy + expected.height / 2, col = dx + expected.width / 2;
            const bool want = row >= 0 && row < expected.height && col >= 0 && col < expected.width &&
                              (expected.rows[row] & (1u << (expected.width - 1 - col)));
            check(isInk(kCx + dx, kCy + dy) == want, "Select pixel must match the hand-drawn Enter bitmap");
          }
      }
      printf("Enter net=%d bbox=%dx%d center=0.0\n", net, expected.width, expected.height);
    }
    printf("%s: Enter bitmaps and footer/inline paths\n", failures ? "RED" : "GREEN");
    return failures ? 1 : 0;
  }

  if (argc == 2 && std::strcmp(argv[1], "--back-reference") == 0) {
    for (const int net : {0, 8, 11, 14, 18}) {
      const auto& expected = net >= 16 ? backV1056::kBackNet18 : backV1056::kBackNet14;
      const auto& actual = net >= 16 ? inlineSymbolBitmaps::kBackNet18 : inlineSymbolBitmaps::kBackNet14;
      check(actual.width == expected.width && actual.height == expected.height,
            "Back dimensions differ from v1.0.56");
      for (int row = 0; row < std::min(actual.height, expected.height); ++row)
        check(actual.rows[row] == expected.rows[row], "Back row differs from v1.0.56");
      for (int path = 0; path < 2; ++path) {
        clear();
        if (path == 0)
          check(buttonSymbols::drawLabel(renderer, "\xEE\x84\x81", kCx, kCy, net), "Back marker refused");
        else
          inlineSymbols::drawShape(renderer, inlineSymbols::Shape::Back, kCx, kCy, net, true);
        const auto bounds = scanAround(kCx, kCy, kRadius);
        check(bounds.any && std::abs(bounds.minY + bounds.maxY - 2 * kCy) <= 1,
              "Back vertical center differs by more than 0.5 pixel");
        for (int dy = -kRadius; dy <= kRadius; ++dy)
          for (int dx = -kRadius; dx <= kRadius; ++dx) {
            const int row = dy + expected.height / 2, column = dx + expected.width / 2;
            const bool ink = row >= 0 && row < expected.height && column >= 0 && column < expected.width &&
                             (expected.rows[row] & (1u << (expected.width - 1 - column)));
            check(isInk(kCx + dx, kCy + dy) == ink, "Back pixel differs from v1.0.56");
          }
      }
    }
    std::printf("%s: v1.0.56 Back bitmaps and 10 footer/inline renders\n", failures ? "RED" : "GREEN");
    return failures ? 1 : 0;
  }

  if (argc == 2 && std::strcmp(argv[1], "--inline-spacing") == 0) {
    const EpdFontData* fonts[] = {&geist_8_regular, &geist_10_regular, &geist_12_regular,
                                &geist_14_regular, &geist_16_regular};
    const char* markers[] = {"\xEE\x84\x81", "\xEE\x84\x80", "\xEE\x84\x82",
                            "\xEE\x84\x83", "\xEE\x84\x84", "\xEE\x84\x85"};
    for (const auto* data : fonts) {
      const EpdFont font(data);
      renderer.insertFont(SMALL_FONT_ID, EpdFontFamily(&font));
      const int space = renderer.getTextAdvanceX(SMALL_FONT_ID, " ", EpdFontFamily::REGULAR);
      for (const char* marker : markers) {
        clear();
        const int advance = inlineSymbols::text(renderer, SMALL_FONT_ID, kCx, kCy, marker, true, true,
                                                EpdFontFamily::REGULAR);
        const auto bounds = scanAround(kCx, kCy, 80);
        const int leftGap = bounds.minX - kCx;
        const int rightGap = kCx + advance - bounds.maxX - 1;
        check(bounds.any && leftGap >= space && rightGap >= space,
              "inline symbol leaves at least one font space on both sides");
        check(advance == inlineSymbols::text(renderer, SMALL_FONT_ID, kCx, kCy, marker, false, true,
                                              EpdFontFamily::REGULAR),
              "inline measurement and painting agree");
        std::printf("font=%d marker=%d space=%d left=%d right=%d\n", data->ascender,
                    static_cast<unsigned char>(marker[2]) - 128, space, leftGap, rightGap);
      }
    }
    std::printf("%s: 30 inline spacing cases\n", failures ? "RED" : "GREEN");
    return failures ? 1 : 0;
  }

  if (argc == 2 && std::strcmp(argv[1], "--family") == 0) {
    const char* markers[] = {"\xEE\x84\x81", "\xEE\x84\x80", "\xEE\x84\x82",
                            "\xEE\x84\x83", "\xEE\x84\x84", "\xEE\x84\x85"};
    for (const int net : {14, 18}) {
      const int span = net == 14 ? 18 : 24;
      const int diagonalRun = net == 14 ? 4 : 6;
      const int stroke = net == 14 ? 3 : 4;
      for (int symbol = 0; symbol < 6; ++symbol) {
        clear();
        check(buttonSymbols::drawLabel(renderer, markers[symbol], kCx, kCy, net), "family marker refused");
        const auto bounds = scanAround(kCx, kCy, kRadius);
        check(bounds.any, "family symbol painted no ink");
        if (!bounds.any) continue;
        const int width = bounds.maxX - bounds.minX + 1;
        const int height = bounds.maxY - bounds.minY + 1;
        const int opticalHeight = symbol == 2 || symbol == 3 ? width : height;
        check(symbol <= 1 || std::abs(opticalHeight - span) <= 1, "family optical span differs by more than 1 pixel");
        check(bounds.minX + bounds.maxX == 2 * kCx - (width % 2 == 0), "family horizontal center moved");
        check(std::abs(bounds.minY + bounds.maxY - 2 * kCy) <= 1, "family vertical center moved");
        const auto cell = buttonSymbols::horizontalBounds(markers[symbol], net);
        const auto painted = snapshot(kCx, kCy, kRadius);
        for (int row = kCy - kRadius; row <= kCy + kRadius; ++row)
          for (int column = kCx - kRadius; column <= kCx + kRadius; ++column)
            if (isInk(column, row))
              check(column >= kCx - cell.left && column <= kCx + cell.right && row >= kCy - span / 2 &&
                        row < kCy + span / 2,
                    "family ink escapes footer cell");
        const auto rowRun = [&](int row, bool fromRight) {
          int column = fromRight ? bounds.maxX : bounds.minX;
          const int step = fromRight ? -1 : 1;
          while (column >= bounds.minX && column <= bounds.maxX && !isInk(column, row)) column += step;
          int count = 0;
          while (column >= bounds.minX && column <= bounds.maxX && isInk(column, row)) {
            ++count;
            column += step;
          }
          return count;
        };
        for (int sample = 0; symbol > 1 && sample < 3; ++sample) {
          int row = bounds.minY + (net == 14 ? (symbol == 0 ? 4 : 5) : 7) + sample;
          if (symbol == 5) row = bounds.maxY - (net == 14 ? 5 : 7) - sample;
          int run = rowRun(row, symbol == 1 || symbol == 5);
          if (symbol == 2 || symbol == 3) {
            const int column =
                symbol == 2 ? bounds.maxX - (row - bounds.minY) : bounds.minX + (row - bounds.minY);
            int sliceRow = bounds.minY;
            while (sliceRow <= bounds.maxY && !isInk(column, sliceRow)) ++sliceRow;
            run = 0;
            while (sliceRow <= bounds.maxY && isInk(column, sliceRow++)) ++run;
          }
          check(run == diagonalRun, "family straight diagonal slice differs from chevron");
        }
        std::printf("family symbol=%d net=%d width=%d height=%d optical=%d center=(%.1f,%.1f) ink=%zu stroke=%d\n",
                    symbol, net, width, height, opticalHeight, (bounds.minX + bounds.maxX) / 2.0 - kCx,
                    (bounds.minY + bounds.maxY) / 2.0 - kCy,
                    static_cast<size_t>(std::count(painted.begin(), painted.end(), 1)), stroke);
        if (net == 14 && symbol < 2) {
          renderer.fillRectDither(kCx - 40, kCy + 9, 80, 3, Color::LightGray);
          for (int row = bounds.minY; row <= bounds.maxY; ++row)
            for (int column = bounds.minX; column <= bounds.maxX; ++column)
              check(isInk(column, row) ==
                        (painted[(row - kCy + kRadius) * (2 * kRadius + 1) + column - kCx + kRadius] != 0),
                    "footer underline clips family glyph");
        }
      }
    }
    std::printf("%s: button family\n", failures ? "RED" : "GREEN");
    return failures ? 1 : 0;
  }

  if (argc == 2 && std::strcmp(argv[1], "--edge-chevrons") == 0) {
    const inlineSymbols::Shape shapes[] = {inlineSymbols::Shape::Left, inlineSymbols::Shape::Right,
                                          inlineSymbols::Shape::Up, inlineSymbols::Shape::Down};
    const char* left[] = {"....##", "...##.", "..##..", ".##...", "##....",
                          ".##...", "..##..", "...##.", "....##"};
    for (int direction = 0; direction < 4; ++direction) {
      clear();
      for (int step = 0; step <= 8; ++step) {
        const int half = step / 2;
        const int sign = direction == 0 || direction == 2 ? -1 : 1;
        if (direction < 2)
          renderer.drawLine(kCx + sign * (4 - step), kCy - half, kCx + sign * (4 - step), kCy + half, true);
        else
          renderer.drawLine(kCx - half, kCy + sign * (4 - step), kCx + half, kCy + sign * (4 - step), true);
      }
      const auto oldBounds = scanAround(kCx, kCy, kRadius);
      const auto oldPixels = snapshot(kCx, kCy, kRadius);
      const int oldInk = std::count(oldPixels.begin(), oldPixels.end(), 1);
      check(oldInk == 41 && oldBounds.maxX - oldBounds.minX == 8 && oldBounds.maxY - oldBounds.minY == 8,
            "old renderer triangle must measure 9x9, 41 pixels");
      for (const int size : {0, 8, 11}) {
        clear();
        inlineSymbols::drawShape(renderer, shapes[direction], kCx, kCy, size, true);
        const auto bounds = scanAround(kCx, kCy, kRadius);
        const int width = direction < 2 ? 6 : 9;
        const int height = direction < 2 ? 9 : 6;
        const int originX = kCx - width / 2, originY = kCy - height / 2;
        for (int offsetY = -12; offsetY <= 12; ++offsetY) {
          for (int offsetX = -12; offsetX <= 12; ++offsetX) {
            const int column = kCx + offsetX - originX, row = kCy + offsetY - originY;
            bool expected = false;
            if (column >= 0 && column < width && row >= 0 && row < height) {
              if (direction == 0) expected = left[row][column] == '#';
              if (direction == 1) expected = left[row][5 - column] == '#';
              if (direction == 2) expected = left[column][row] == '#';
              if (direction == 3) expected = left[column][5 - row] == '#';
            }
            check(isInk(kCx + offsetX, kCy + offsetY) == expected, "edge chevron differs from hand-drawn pixel");
          }
        }
        check(inlineSymbols::glyphWidth(shapes[direction], size) == width, "edge glyphWidth differs from bitmap");
        check(bounds.any && bounds.maxX - bounds.minX + 1 == width && bounds.maxY - bounds.minY + 1 == height,
              "edge bounding box differs from hand-drawn dimensions");
        check(bounds.minX >= oldBounds.minX && bounds.maxX <= oldBounds.maxX &&
                  bounds.minY >= oldBounds.minY && bounds.maxY <= oldBounds.maxY,
              "edge chevron escapes old triangle bounds");
        check(direction < 2 ? bounds.minY + bounds.maxY == 2 * kCy : bounds.minX + bounds.maxX == 2 * kCx,
              "edge chevron transverse center moved");
        for (int slice = 0; slice < 9; ++slice) {
          int ink = 0;
          for (int along = 0; along < 6; ++along) {
            const int pixelX = originX + (direction < 2 ? along : slice);
            const int pixelY = originY + (direction < 2 ? slice : along);
            ink += isInk(pixelX, pixelY);
            check(isInk(pixelX, pixelY) ==
                      isInk(direction < 2 ? pixelX : originX + 8 - slice,
                            direction < 2 ? originY + 8 - slice : pixelY),
                  "edge chevron lost axial symmetry");
          }
          check(ink == 2, "edge chevron slice must contain exactly 2 pixels");
        }
        std::printf("direction=%d size=%d old=9x9 ink=%d new=%dx%d transverse_shift=0\n",
                    direction, size, oldInk, width, height);
        inlineSymbols::drawShape(renderer, shapes[direction], kCx, kCy, size, false);
        check(!scanAround(kCx, kCy, kRadius).any, "white edge chevron left ink behind");
      }
      if (direction < 2) {
        clear();
        for (int step = 0; step <= 6; ++step) {
          const int halfHeight = step * 4 / 6;
          const int column = direction == 0 ? kCx - 3 + step : kCx + 3 - step;
          renderer.drawLine(column, kCy - halfHeight, column, kCy + halfHeight, true);
        }
        const auto narrowBounds = scanAround(kCx, kCy, kRadius);
        const auto narrowPixels = snapshot(kCx, kCy, kRadius);
        check(narrowBounds.maxX - narrowBounds.minX == 6 && narrowBounds.maxY - narrowBounds.minY == 8 &&
                  std::count(narrowPixels.begin(), narrowPixels.end(), 1) == 31,
              "old UiList triangle must measure 7x9, 31 pixels");
        clear();
        inlineSymbols::drawShape(renderer, shapes[direction], kCx, kCy, 8, true);
        const auto newBounds = scanAround(kCx, kCy, kRadius);
        check(newBounds.minX >= narrowBounds.minX && newBounds.maxX <= narrowBounds.maxX &&
                  newBounds.minY == narrowBounds.minY && newBounds.maxY == narrowBounds.maxY,
              "edge chevron escapes UiList triangle or moves its transverse center");
        std::printf("UiList direction=%d old=7x9 ink=31 new=6x9 transverse_shift=0\n", direction);
      }
    }
    std::printf("%s: hand-drawn edge chevrons\n", failures ? "RED" : "GREEN");
    return failures ? 1 : 0;
  }

  if (argc == 2 && std::strcmp(argv[1], "--chevrons") == 0) {
    const inlineSymbols::Shape shapes[] = {inlineSymbols::Shape::Up, inlineSymbols::Shape::Down,
                                          inlineSymbols::Shape::Left, inlineSymbols::Shape::Right};
    const char* markers[] = {"\xEE\x84\x82", "\xEE\x84\x83", "\xEE\x84\x84", "\xEE\x84\x85"};
    for (const int net : {14, 18}) {
      for (int direction = 0; direction < 4; ++direction) {
        clear();
        check(buttonSymbols::drawLabel(renderer, markers[direction], kCx, kCy, net), "chevron marker refused");
        const auto bounds = scanAround(kCx, kCy, kRadius);
        check(bounds.any, "chevron painted no ink");
        if (!bounds.any) continue;
        const auto painted = snapshot(kCx, kCy, kRadius);
        const int ink = std::count(painted.begin(), painted.end(), 1);
        const int area = (bounds.maxX - bounds.minX + 1) * (bounds.maxY - bounds.minY + 1);
        std::printf("direction=%d net=%d ink=%d area=%d density=%.2f%%\n", direction, net, ink, area,
                    100.0 * ink / area);
        check(ink * 100 < area * 50, "chevron ink density must be below 50 percent");
        const auto expected = buttonSymbols::horizontalBounds(markers[direction], net);
        check(kCx - bounds.minX == expected.left && bounds.maxX - kCx == expected.right,
              "chevron horizontalBounds must exactly match painted ink");
        check(bounds.minY + (bounds.maxY - bounds.minY + 1) / 2 == kCy, "chevron vertical center changed");
        clear();
        inlineSymbols::drawShape(renderer, shapes[direction], kCx, kCy, net, true);
        check(snapshot(kCx, kCy, kRadius) == painted, "footer and inline chevron pixels differ");
        inlineSymbols::drawShape(renderer, shapes[direction], kCx, kCy, net, false);
        const auto erased = snapshot(kCx, kCy, kRadius);
        check(std::count(erased.begin(), erased.end(), 1) == 0, "white chevron did not erase its ink");
      }
    }
    using inlineSymbolBitmaps::Glyph;
    const Glyph* selected[] = {&inlineSymbolBitmaps::kBackNet14, &inlineSymbolBitmaps::kBackNet18,
                               &inlineSymbolBitmaps::kSelectNet14, &inlineSymbolBitmaps::kSelectNet18};
    const uint32_t hashes[] = {0x20785c88, 0x14450811, 0x9fbaa395, 0x2bf6fa4b};
    const int widths[] = {24, 31, 22, 29};
    const int heights[] = {15, 19, 15, 19};
    for (int index = 0; index < 4; ++index) {
      const auto& glyph = *selected[index];
      uint32_t hash = 2166136261u;
      for (int row = 0; row < glyph.height; ++row) hash = (hash ^ glyph.rows[row]) * 16777619u;
      check(hash == hashes[index] && glyph.width == widths[index] && glyph.height == heights[index],
            "Back/Select reference bitmap changed");
      clear();
      const int net = index % 2 == 0 ? 14 : 18;
      const bool back = index < 2;
      buttonSymbols::drawLabel(renderer, back ? "\xEE\x84\x81" : "\xEE\x84\x80", kCx, kCy, net);
      for (int offsetY = -kRadius; offsetY <= kRadius; ++offsetY) {
        for (int offsetX = -kRadius; offsetX <= kRadius; ++offsetX) {
          const int row = offsetY + glyph.height / 2;
          const int column = offsetX + glyph.width / 2;
          const bool expected = row >= 0 && row < glyph.height && column >= 0 && column < glyph.width &&
                                (glyph.rows[row] & (1u << (glyph.width - 1 - column)));
          check(isInk(kCx + offsetX, kCy + offsetY) == expected, "Back/Select footer pixels changed");
        }
      }
    }
    std::printf("%s: chevron density, bounds, centering and selected Back/Select\n", failures ? "RED" : "GREEN");
    return failures ? 1 : 0;
  }

  using Oracle = void (*)(const GfxRenderer&, int, int, int, bool);
  const auto checkShape = [&](const char* name, inlineSymbols::Shape shape, const char* marker, Oracle oracle,
                               bool enforceOldBounds) {
    for (const int net : {14, 18}) {
      clear();
      oracle(renderer, kCx, kCy, net, true);
      const auto oldBounds = scanAround(kCx, kCy, kRadius);
      const auto oldSnapshot = snapshot(kCx, kCy, kRadius);
      char oracleMsg[96];
      std::snprintf(oracleMsg, sizeof(oracleMsg), "%s net=%d: oracle painted no ink", name, net);
      check(oldBounds.any, oracleMsg);

      clear();
      inlineSymbols::drawShape(renderer, shape, kCx, kCy, net, true);
      const auto newBounds = scanAround(kCx, kCy, kRadius);
      const auto newSnapshot = snapshot(kCx, kCy, kRadius);
      char paintedMsg[96];
      std::snprintf(paintedMsg, sizeof(paintedMsg), "%s net=%d: new shape painted no ink", name, net);
      check(newBounds.any, paintedMsg);

      const auto expected = buttonSymbols::horizontalBounds(marker, net);
      if (newBounds.any) {
        const int actualLeft = kCx - newBounds.minX;
        const int actualRight = newBounds.maxX - kCx;
        char msg[180];
        std::snprintf(msg, sizeof(msg), "%s net=%d: horizontalBounds {%d,%d} does not match ink {%d,%d}", name, net,
                      expected.left, expected.right, actualLeft, actualRight);
        check(actualLeft == expected.left && actualRight == expected.right, msg);

        if (enforceOldBounds) {
          char boundsMsg[220];
          std::snprintf(boundsMsg, sizeof(boundsMsg),
                        "%s net=%d: new ink y=[%d,%d] escapes the old glyph's proven-safe y=[%d,%d]", name, net,
                        newBounds.minY - kCy, newBounds.maxY - kCy, oldBounds.minY - kCy, oldBounds.maxY - kCy);
          check(newBounds.minY >= oldBounds.minY && newBounds.maxY <= oldBounds.maxY, boundsMsg);
        }
      }

      char msg[110];
      std::snprintf(msg, sizeof(msg), "%s net=%d: new shape is pixel-identical to the old glyph it replaced", name,
                    net);
      check(newSnapshot != oldSnapshot, msg);

      // Same shape through the real footer entry point (ButtonSymbols::drawLabel),
      // which may apply its own small vertical nudge but must keep the same
      // horizontal extent that horizontalBounds() promises the footer.
      clear();
      char refusedMsg[96];
      std::snprintf(refusedMsg, sizeof(refusedMsg), "%s net=%d: drawLabel refused the marker", name, net);
      check(buttonSymbols::drawLabel(renderer, marker, kCx, kCy, net), refusedMsg);
      const auto footerBounds = scanAround(kCx, kCy, kRadius);
      if (footerBounds.any) {
        const int actualLeft = kCx - footerBounds.minX;
        const int actualRight = footerBounds.maxX - kCx;
        char msg2[190];
        std::snprintf(msg2, sizeof(msg2), "%s net=%d: footer drawLabel ink {%d,%d} escapes horizontalBounds {%d,%d}",
                      name, net, actualLeft, actualRight, expected.left, expected.right);
        check(actualLeft <= expected.left && actualRight <= expected.right, msg2);
      } else {
        char noPaintMsg[96];
        std::snprintf(noPaintMsg, sizeof(noPaintMsg), "%s net=%d: footer drawLabel painted no ink", name, net);
        check(false, noPaintMsg);
      }
    }
  };

  // Back is inlineSymbols::Shape id 1, Select is id 0; marker() decodes
  // 0xEE 0x84 0x80+id as that.
  checkShape("Back", inlineSymbols::Shape::Back, "\xEE\x84\x81", drawOldBackShape, false);
  checkShape("Select", inlineSymbols::Shape::Select, "\xEE\x84\x80", drawOldSelectShape,
             /*enforceOldBounds=*/false);

  // Dong da ghim (Yeu thich) mang dau tim 9x8 o le trai. Ma E10A trong inlineSymbols::text
  // goi dung ham nay.
  {
    clear();
    constexpr int kRowY = 300;
    inlineSymbols::drawMarginPin(renderer, kRowY - 4);
    static const char* const kTim[] = {".##...##.", "####.####", "#########", "#########",
                                       ".#######.", "..#####..", "...###...", "....#...."};
    const auto ink = scanAround(17, kRowY, 30);
    check(ink.any && ink.maxX - ink.minX == 8 && ink.maxY - ink.minY == 7, "dau ghim: khung muc khong phai 9x8");
    bool khop = ink.any;
    for (int dy = 0; dy < 8 && khop; ++dy)
      for (int dx = 0; dx < 9; ++dx)
        if (isInk(ink.minX + dx, ink.minY + dy) != (kTim[dy][dx] == '#')) khop = false;
    check(khop, "dau ghim: hinh khong phai trai tim");
    SETTINGS.tenorSideArrows = 0;
    clear();
    inlineSymbols::drawMarginPin(renderer, kRowY - 4);
    const auto large = scanAround(17, kRowY, 30);
    const int expectedW = tenorchrome::kTouchShell ? 9 : 17, expectedH = tenorchrome::kTouchShell ? 8 : 15;
    check(large.any && large.maxX - large.minX == expectedW - 1 && large.maxY - large.minY == expectedH - 1,
          "arrows off: pin heart must grow to 17x15");
    check(large.any && large.maxX == 21 && large.minX >= 0, "large pin must stay in the left margin");
    SETTINGS.tenorSideArrows = 1;
  }

  std::printf(
      "%s: Back arrow and Select tick ink bounds match horizontalBounds() and differ from the glyphs they "
      "replaced, at net 14 and 18\n",
      failures ? "RED" : "GREEN");
  return failures ? 1 : 0;
}
