// The X3 sleep dither (SleepGrayPlanes) through the real GfxRenderer into a RAM framebuffer. With
// no heap to keep the B/W frame of an overlay picture, base() shows that frame at once. The gray
// nudge planes its caller would draw next have nowhere to go: drawn anyway, they overwrite the
// framebuffer, which then no longer matches the glass. base() says so, and every caller in
// SleepActivity stops there.
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "activities/boot_sleep/SleepGrayPlanes.h"

namespace {

int failures = 0;
void check(const bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE, 0xff);
std::vector<uint8_t> glass;
int shows = 0;
bool noHeap = false;

}  // namespace

// The dither keeps the B/W frame with nothrow allocations: this is where the heap runs out.
void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
  return noHeap ? nullptr : std::malloc(size);
}

HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
void HalDisplay::displayBuffer(RefreshMode, bool) {
  glass = pixels;
  ++shows;
}
void HalDisplay::clearScreen(const uint8_t color) const { std::fill(pixels.begin(), pixels.end(), color); }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void HalDisplay::displayGrayscaleBase(RefreshMode, bool) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(bool) {}

namespace {

// An overlay sleep picture the way SleepActivity draws it: the B/W frame, base(), then the two
// nudge planes, each starting all clear, then show(). The caller stops when base() says the frame
// is already on the glass.
void drawOverlay(GfxRenderer& renderer, const bool heap) {
  std::fill(pixels.begin(), pixels.end(), 0x00);  // the B/W frame: a black picture, a white band
  std::fill(pixels.begin(), pixels.begin() + pixels.size() / 4, 0xff);
  const std::vector<uint8_t> frame = pixels;
  shows = 0;
  glass.clear();
  SleepGrayPlanes planes(renderer, true);
  noHeap = !heap;
  const bool drawPlanes = planes.base();
  noHeap = false;
  if (drawPlanes) {
    for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      renderer.clearScreen(0x00);
      renderer.setRenderMode(mode);
      std::fill(pixels.end() - pixels.size() / 4, pixels.end(), 0xff);  // a gray patch on the black
      if (mode == GfxRenderer::GRAYSCALE_LSB) planes.lsb();
    }
    planes.show();
  }
  renderer.setRenderMode(GfxRenderer::BW);
  check(shows == 1, "one B/W refresh");
  check(pixels == glass, "the framebuffer matches the glass");
  if (!heap) check(!drawPlanes && glass == frame, "without heap the frame is shown as drawn, planes skipped");
  if (heap) check(drawPlanes && glass != frame, "with heap the planes are folded into the frame");
}

// Every base() in SleepActivity must be asked, and the drawing stop when it answers false.
void checkCallers() {
  std::ifstream file(REPO_ROOT_PATH "/src/activities/boot_sleep/SleepActivity.cpp");
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  int calls = 0, guarded = 0;
  for (size_t at = text.find("planes.base()"); at != std::string::npos; at = text.find("planes.base()", at + 1)) {
    ++calls;
    guarded += text.compare(at - 5, 5, "if (!") == 0 ? 1 : 0;
  }
  std::printf("SleepActivity: %d base() calls, %d stop when it answers false\n", calls, guarded);
  check(calls > 0 && guarded == calls, "every caller stops when base() has shown the frame");
}

}  // namespace

int main() {
  HalDisplay display;
  GfxRenderer renderer{display};
  renderer.begin();
  drawOverlay(renderer, true);
  drawOverlay(renderer, false);
  checkCallers();
  if (failures) return 1;
  std::puts("PASS: an overlay sleep picture leaves the framebuffer as the glass shows it");
  return 0;
}
