#include <GfxRenderer.h>
#include <HalDisplay.h>
#include "components/HomeCoverCorners.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE, 0xff);
HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
void HalDisplay::displayBuffer(RefreshMode, bool) {}
void HalDisplay::clearScreen(uint8_t c) const { std::fill(pixels.begin(), pixels.end(), c); }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void HalDisplay::displayGrayscaleBase(RefreshMode, bool) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(bool) {}
bool HalDisplay::isInverted() const { return false; }

bool ink(int x, int y) {
  return !(pixels[y * HalDisplay::DISPLAY_WIDTH_BYTES + x / 8] & (0x80 >> (x % 8)));
}

int main(int argc, char**) {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  int cases = 0;
  for (int w : {126, 128, 180, 253, 269, 270, 281, 298, 300})
    for (int h : {172, 193, 408}) {
      std::fill(pixels.begin(), pixels.end(), 0xff);
      homeCoverCorners(renderer, 24, 52, w, h);
      if (argc > 1) renderer.drawRect(24, 52, w, h, true);
      for (int x = 24; x < 24 + w; ++x) assert(!ink(x, 52) && !ink(x, 51 + h));
      for (int y = 52; y < 52 + h; ++y) assert(!ink(24, y) && !ink(23 + w, y));
      renderer.fillRect(24, 52, w, h, true);
      homeCoverCorners(renderer, 24, 52, w, h);
      assert(!ink(24, 52) && !ink(23 + w, 51 + h));
      assert(ink(24 + w / 2, 52 + h / 2));
      ++cases;
    }
  std::printf("GREEN cover edges and rounded corners cases=%d\n", cases);
}
