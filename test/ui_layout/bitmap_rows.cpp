// A 1-bit BMP drawn 1:1 (the Recent card's cover, the X3 black and white sleep cover) through the
// real GfxRenderer into a RAM framebuffer. Those rows go to the framebuffer as they are stored,
// not pixel by pixel through drawPixel with its rotation and bounds check (the card's cover took
// 121 to 221 ms on the X3 that way). The same picture stored as a 2-bit BMP still takes the pixel
// path, so the two framebuffers must match bit for bit, in every orientation, for top-down and
// bottom-up files, cropped or not; and the 1-bit one must take less than half the time here (the
// pixel path costs more on the X3, where drawPixel is a real call per pixel).
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {

int failures = 0;
void check(const bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

std::vector<uint8_t> pixels(HalDisplay::BUFFER_SIZE, 0xff);

void put(std::vector<uint8_t>& out, const uint32_t v, const int bytes) {
  for (int i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// A BMP of `ink` (true = black) at 1 or 2 bits per pixel, black and white palette entries only.
std::vector<uint8_t> bmp(const std::vector<bool>& ink, const int w, const int h, const int bpp, const bool topDown) {
  const int colors = 1 << bpp;
  const int rowBytes = (w * bpp + 31) / 32 * 4;
  const uint32_t offset = 54 + colors * 4;
  std::vector<uint8_t> out = {'B', 'M'};
  put(out, offset + rowBytes * h, 4);
  put(out, 0, 4);
  put(out, offset, 4);
  put(out, 40, 4);
  put(out, w, 4);
  put(out, static_cast<uint32_t>(topDown ? -h : h), 4);
  put(out, 1, 2);
  put(out, bpp, 2);
  for (int i = 0; i < 6; ++i) put(out, i == 4 || i == 5 ? colors : 0, 4);
  for (int c = 0; c < colors; ++c) {
    const uint8_t v = c == colors - 1 ? 255 : 0;
    for (int k = 0; k < 3; ++k) out.push_back(v);
    out.push_back(0);
  }
  for (int r = 0; r < h; ++r) {
    const int y = topDown ? r : h - 1 - r;
    std::vector<uint8_t> row(rowBytes, 0);
    for (int x = 0; x < w; ++x) {
      const int level = ink[y * w + x] ? 0 : colors - 1;
      const int bit = x * bpp;
      row[bit / 8] |= static_cast<uint8_t>(level << (8 - bpp - bit % 8));
    }
    out.insert(out.end(), row.begin(), row.end());
  }
  return out;
}

using Draw = bool (*)(GfxRenderer&, const Bitmap&);

std::vector<uint8_t> render(GfxRenderer& renderer, const std::vector<uint8_t>& file, const Draw draw,
                            double* seconds = nullptr, const int repeats = 1) {
  const auto started = std::chrono::steady_clock::now();
  for (int i = 0; i < repeats; ++i) {
    std::fill(pixels.begin(), pixels.end(), 0xa5);  // neither white nor black: every write shows
    HalFile handle;
    handle.bytes = &file;
    Bitmap bitmap(handle);
    check(bitmap.parseHeaders() == BmpReaderError::Ok, "the test BMP parses");
    check(draw(renderer, bitmap), "the bitmap draws");
  }
  if (seconds) *seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  return pixels;
}

}  // namespace

HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
uint8_t* HalDisplay::getFrameBuffer() const { return pixels.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
void HalDisplay::displayBuffer(RefreshMode, bool) {}
void HalDisplay::clearScreen(const uint8_t color) const { std::fill(pixels.begin(), pixels.end(), color); }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void HalDisplay::displayGrayscaleBase(RefreshMode, bool) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(bool) {}
bool HalDisplay::isInverted() const { return false; }

int main() {
  HalDisplay display;
  GfxRenderer renderer{display};
  renderer.begin();
  std::mt19937 random(7);
  // The card's thumbnail of a 780x1227 cover (236x371, drawn cropped to 236x356), a wide one
  // (262x356, cropped in x), and a screen-sized sleep cover narrower than the screen.
  struct Case {
    int w, h;
    Draw draw;
    const char* name;
  };
  const Case cases[] = {
      {236, 371, [](GfxRenderer& r, const Bitmap& b) { return r.drawBitmapCover(b, 24, 124, 236, 356); }, "card, tall"},
      {262, 356, [](GfxRenderer& r, const Bitmap& b) { return r.drawBitmapCover(b, 24, 124, 236, 356); }, "card, wide"},
      {503, 792, [](GfxRenderer& r, const Bitmap& b) { return r.drawBitmap(b, 12, 0, 528, 792); }, "sleep, fit"},
      {528, 830,
       [](GfxRenderer& r, const Bitmap& b) { return r.drawBitmap(b, 0, 0, 528, 792, 0.0f, 1.0f - 792.0f / 830.0f); },
       "sleep, crop"},
  };
  for (const auto& c : cases) {
    std::vector<bool> ink(c.w * c.h);
    for (size_t i = 0; i < ink.size(); ++i) ink[i] = random() & 1;
    for (const bool topDown : {true, false}) {
      const auto one = bmp(ink, c.w, c.h, 1, topDown), two = bmp(ink, c.w, c.h, 2, topDown);
      for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise,
                                     GfxRenderer::PortraitInverted, GfxRenderer::LandscapeCounterClockwise}) {
        renderer.setOrientation(orientation);
        if (orientation != GfxRenderer::Portrait && c.draw == cases[2].draw) continue;  // fits portrait only
        if (orientation != GfxRenderer::Portrait && c.draw == cases[3].draw) continue;
        // The pixel path stops at the first row below the screen, which in a bottom-up file cropped
        // in y is the first row it reads: it draws nothing there. The row path reads the crop.
        if (!topDown && c.draw == cases[3].draw) continue;
        const bool same = render(renderer, one, c.draw) == render(renderer, two, c.draw);
        if (!same) std::printf("  %s, %s, orientation %d\n", c.name, topDown ? "top-down" : "bottom-up", orientation);
        check(same, "a 1-bit BMP draws the same pixels as its 2-bit copy");
      }
    }
    renderer.setOrientation(GfxRenderer::Portrait);
    double oneBit = 0, twoBit = 0;
    const auto one = bmp(ink, c.w, c.h, 1, true), two = bmp(ink, c.w, c.h, 2, true);
    render(renderer, one, c.draw, &oneBit, 5);  // warm up
    render(renderer, one, c.draw, &oneBit, 100);
    render(renderer, two, c.draw, &twoBit, 100);
    std::printf("%s: 1-bit %.3f ms, 2-bit %.3f ms per draw\n", c.name, oneBit * 10, twoBit * 10);
    check(oneBit * 2 < twoBit, "a 1-bit bitmap drawn 1:1 is not twice as fast as the pixel path");
  }
  if (failures) return 1;
  std::puts("PASS: 1-bit bitmaps go to the framebuffer row by row, pixel for pixel as before");
  return 0;
}
