// Thin rounded borders (drawRoundedRect with a 1 to 3 px line) through the real GfxRenderer into a
// RAM framebuffer. A border must read as one unbroken line: every pixel of it touches the next
// (8-connected), none lies outside the filled block of the same size, and it closes, so the inside
// cannot leak out through a gap where the straight edge meets the curve.
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE, 0xff);
void clear() { std::fill(pixels.begin(), pixels.end(), 0xff); }
bool isInk(const int x, const int y) {
  const uint8_t byte = pixels[static_cast<size_t>(y) * HalDisplay::DISPLAY_WIDTH_BYTES + x / 8];
  return ((byte >> (7 - (x % 8))) & 1) == 0;
}

constexpr int X = 40, Y = 40, W = 300, H = 140, M = 2;  // M: margin read around the block
constexpr int GW = W + 2 * M, GH = H + 2 * M;

std::vector<char> grab() {
  std::vector<char> g(GW * GH);
  for (int i = 0; i < GH; ++i)
    for (int j = 0; j < GW; ++j) g[i * GW + j] = isInk(X - M + j, Y - M + i);
  return g;
}

// Cells of `g` equal to `want` reachable from (sx, sy), stepping to the
// 8 neighbours when `eight`, else to the 4.
std::vector<char> flood(const std::vector<char>& g, const int sx, const int sy, const bool eight, const bool want) {
  std::vector<char> seen(GW * GH, 0);
  std::vector<int> stack{sy * GW + sx};
  seen[sy * GW + sx] = 1;
  while (!stack.empty()) {
    const int at = stack.back();
    stack.pop_back();
    const int y = at / GW, x = at % GW;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        if ((dx == 0 && dy == 0) || (!eight && dx != 0 && dy != 0)) continue;
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) continue;
        const int n = ny * GW + nx;
        if (seen[n] || g[n] != want) continue;
        seen[n] = 1;
        stack.push_back(n);
      }
  }
  return seen;
}

void dumpCorner(const std::vector<char>& ring, const std::vector<char>& fill) {
  for (int i = 0; i < 50; ++i) {
    for (int j = 0; j < 60; ++j) {
      const int n = i * GW + j;
      std::putchar(ring[n] ? (fill[n] ? '#' : '!') : (fill[n] ? '.' : ' '));
    }
    std::putchar('\n');
  }
}

void checkBorder(const GfxRenderer& renderer, const int r, const int lw) {
  clear();
  renderer.fillRoundedRect(X, Y, W, H, r, Color::Black);
  const auto fill = grab();
  clear();
  renderer.drawRoundedRect(X, Y, W, H, lw, r, true);
  const auto ring = grab();

  int outside = 0, inked = 0, first = -1;
  for (int n = 0; n < GW * GH; ++n) {
    if (!ring[n]) continue;
    ++inked;
    if (first < 0) first = n;
    if (!fill[n]) ++outside;
  }
  // One line: every inked pixel is reached from the first through 8-neighbours.
  const auto line = flood(ring, first % GW, first / GW, true, true);
  int reached = 0;
  for (const char c : line) reached += c;
  // Closed: from the middle of the block, stepping over paper (4-neighbours, the way a gap one
  // pixel wide on a diagonal still leaks), the inside never reaches the paper around the block.
  const auto inside = flood(ring, GW / 2, GH / 2, false, false);
  const bool leaks = inside[0] != 0;
  if (outside || reached != inked || leaks) {
    std::printf("FAIL: border r=%d lw=%d: %d px outside the fill, %d of %d px on one line, %s\n", r, lw, outside,
                reached, inked, leaks ? "the inside leaks out" : "closed");
    dumpCorner(ring, fill);
    ++failures;
  }
}

}  // namespace

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
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);  // identity mapping
  // Every radius the scale draws, not only the ones on screen today (7, 8, 11, 27): sampled at
  // pixel centres alone, a 33 px corner with a 1 px line still breaks where the curve flattens.
  for (int r = 2; r <= 40; ++r)
    for (const int lw : {1, 2, 3}) checkBorder(renderer, r, lw);
  if (failures) return 1;
  std::puts("PASS: thin rounded borders are one closed 8-connected line inside their filled block");
  return 0;
}
