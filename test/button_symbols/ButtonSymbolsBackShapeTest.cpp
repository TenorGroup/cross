// Renders the production Back-arrow and Select-tick shapes (both now the
// fixed 1-bit bitmaps, see InlineSymbolBitmaps.h) into a real
// GfxRenderer framebuffer and checks two things end to end for each:
//   1. ButtonSymbols::horizontalBounds still reports the true ink extent, so
//      the footer's cell-clearing and corner clamping stay correct.
//   2. The new shape's pixels differ from the glyph it replaced.
// Follows the sd_font_lifetime recipe: HalDisplay's frame buffer is a plain
// RAM vector, so the real GfxRenderer paints into memory this test can read.
#include <ButtonSymbols.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <InlineSymbols.h>

#include <algorithm>
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

int main() {
  HalDisplay display;
  GfxRenderer renderer{display};
  renderer.begin();
  // Identity coordinate mapping (phyX=x, phyY=y), so ink bounds read directly
  // in the same x the shape functions and horizontalBounds() use.
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);

  constexpr int kCx = 200, kCy = 200;
  constexpr int kRadius = 40;

  using Oracle = void (*)(const GfxRenderer&, int, int, int, bool);
  // Runs the full oracle/bitmap/footer comparison for one bitmap-backed
  // shape. `oracle` reproduces the glyph this shape replaced, so the test
  // can prove the new render differs from it. `enforceOldBounds` additionally
  // requires staying inside the old glyph's own ink box: true for Back, whose
  // bitmap fits it exactly. Select's tick bitmap is a full
  // symmetric box, taller below center than the old tapered checkmark, so it
  // legitimately exceeds that box; the real constraint (the footer layout's
  // reserved caption gap) is instead re-measured and asserted directly in
  // test_fixed_menu_chrome.py in the simulator suite.
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
  checkShape("Back", inlineSymbols::Shape::Back, "\xEE\x84\x81", drawOldBackShape, /*enforceOldBounds=*/true);
  checkShape("Select", inlineSymbols::Shape::Select, "\xEE\x84\x80", drawOldSelectShape,
             /*enforceOldBounds=*/false);

  std::printf(
      "%s: Back arrow and Select tick ink bounds match horizontalBounds() and differ from the glyphs they "
      "replaced, at net 14 and 18\n",
      failures ? "RED" : "GREEN");
  return failures ? 1 : 0;
}
