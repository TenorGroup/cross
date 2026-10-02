// Cover decodes on the real JPEGDEC, as the X3 runs them.
// 1. JPEGDEC decodes straight to a half, quarter or eighth of a baseline JPEG. Every JPEG the
//    firmware draws takes the largest such reduction that still covers its output (JpegScale.h);
//    the screen-sized cover of the sleep screen decoded 2-bit at full size and kept a row buffer
//    as wide as the source.
// 2. A book's two cover thumbnails (the card's 356 px and the theme's 226 px) came from two decodes
//    of the cover when the reader closed: 3,4 s and 0,9 s for a 900 x 1350 cover on the X3. One
//    decode now feeds both (GrayThumb). The card's thumbnail keeps the pixels of its own decode, and
//    the theme's is area averaged from the decoded gray: scaling it from the card's dithered bits
//    dithered it twice.
#include <Arduino.h>
#include <GrayThumb.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <JpegScale.h>
#include <JpegToBmpConverter.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "BitmapHelpers.h"

EspClass ESP;

namespace {
bool tracking = false;
std::vector<size_t> arrays;
// Bytes live now and at most since `peak` was reset, for what a decode holds at once.
size_t live = 0, peak = 0;
constexpr size_t HEAD = 16;
void* take(size_t size, bool array) {
  if (tracking && array) arrays.push_back(size);
  auto* p = static_cast<unsigned char*>(std::malloc(size + HEAD));
  if (!p) return nullptr;
  std::memcpy(p, &size, sizeof(size));
  live += size;
  peak = std::max(peak, live);
  return p + HEAD;
}
void give(void* p) {
  if (!p) return;
  auto* head = static_cast<unsigned char*>(p) - HEAD;
  size_t size;
  std::memcpy(&size, head, sizeof(size));
  live -= size;
  std::free(head);
}
}  // namespace

void* operator new(size_t size) {
  if (void* p = take(size, false)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t size) {
  if (void* p = take(size, true)) return p;
  throw std::bad_alloc();
}
void* operator new(size_t size, const std::nothrow_t&) noexcept { return take(size, false); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return take(size, true); }
void operator delete(void* p) noexcept { give(p); }
void operator delete[](void* p) noexcept { give(p); }
void operator delete(void* p, size_t) noexcept { give(p); }
void operator delete[](void* p, size_t) noexcept { give(p); }

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::printf("FAIL %s\n", what);
  }
}

std::vector<uint8_t> fixture(const char* name) {
  std::ifstream in(std::string(FIXTURES) + "/" + name, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

class Output : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t write(uint8_t v) override { return write(&v, 1); }
  size_t write(const uint8_t* data, size_t n) override {
    bytes.insert(bytes.end(), data, data + n);
    return n;
  }
};

int32_t le32(const std::vector<uint8_t>& b, int at) {
  return static_cast<int32_t>(b[at] | b[at + 1] << 8 | b[at + 2] << 16 | static_cast<uint32_t>(b[at + 3]) << 24);
}

// A 1-bit BMP as gray: 255 for a set (white) bit.
struct Gray {
  int w = 0, h = 0;
  std::vector<int> px;
};
Gray fromBmp(const std::vector<uint8_t>& b) {
  Gray g;
  if (b.size() < 62) return g;
  g.w = le32(b, 18);
  g.h = -le32(b, 22);
  const int stride = (g.w + 31) / 32 * 4;
  if (g.w <= 0 || g.h <= 0 || b.size() != 62 + static_cast<size_t>(stride) * g.h) return {};
  g.px.resize(static_cast<size_t>(g.w) * g.h);
  for (int y = 0; y < g.h; y++)
    for (int x = 0; x < g.w; x++) g.px[y * g.w + x] = (b[62 + y * stride + x / 8] >> (7 - x % 8)) & 1 ? 255 : 0;
  return g;
}

// The cover at full size, decoded by JPEGDEC from RAM, for the reference below.
Gray fullDecode(std::vector<uint8_t> jpeg) {
  static Gray out;
  JPEGDEC dec;
  out = {};
  if (!dec.openRAM(jpeg.data(), static_cast<int>(jpeg.size()), [](JPEGDRAW* d) {
        const auto* p = reinterpret_cast<const uint8_t*>(d->pPixels);
        for (int r = 0; r < d->iHeight && d->y + r < out.h; r++)
          for (int c = 0; c < d->iWidthUsed && d->x + c < out.w; c++)
            out.px[(d->y + r) * out.w + d->x + c] = p[r * d->iWidth + c];
        return 1;
      }))
    return {};
  out.w = dec.getWidth();
  out.h = dec.getHeight();
  out.px.assign(static_cast<size_t>(out.w) * out.h, 0);
  dec.setPixelType(EIGHT_BIT_GRAYSCALE);
  if (dec.decode(0, 0, 0) != 1) out = {};
  dec.close();
  return out;
}

// The cover area averaged to w x h: what a thumbnail stands for once its dither is blurred away.
Gray areaAverage(const Gray& src, int w, int h) {
  Gray g{w, h, std::vector<int>(static_cast<size_t>(w) * h)};
  for (int oy = 0; oy < h; oy++)
    for (int ox = 0; ox < w; ox++) {
      const int x0 = ox * src.w / w, x1 = std::max(x0 + 1, (ox + 1) * src.w / w);
      const int y0 = oy * src.h / h, y1 = std::max(y0 + 1, (oy + 1) * src.h / h);
      long sum = 0;
      for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) sum += src.px[y * src.w + x];
      g.px[oy * w + ox] = static_cast<int>(sum / ((x1 - x0) * (y1 - y0)));
    }
  return g;
}

Gray blur(const Gray& g) {
  Gray b{g.w, g.h, std::vector<int>(g.px.size())};
  for (int y = 0; y < g.h; y++)
    for (int x = 0; x < g.w; x++) {
      int sum = 0, n = 0;
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
          const int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= g.w || yy >= g.h) continue;
          sum += g.px[yy * g.w + xx];
          n++;
        }
      b.px[y * g.w + x] = sum / n;
    }
  return b;
}

// Mean difference of a thumbnail and the cover it stands for, seen at reading distance.
double seenError(const std::vector<uint8_t>& bmp, const Gray& cover) {
  const Gray thumb = fromBmp(bmp);
  if (thumb.px.empty()) return 1e9;
  const Gray a = blur(thumb), b = blur(areaAverage(cover, thumb.w, thumb.h));
  double sum = 0;
  for (size_t i = 0; i < a.px.size(); i++) sum += std::abs(a.px[i] - b.px[i]);
  return sum / static_cast<double>(a.px.size());
}

std::vector<uint8_t> fileThumb(const std::vector<uint8_t>& jpeg, int height) {
  HalFile file(jpeg);
  Output out;
  if (!JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(file, out, thumbWidthFor(height), height)) return {};
  return out.bytes;
}

bool has(const std::vector<size_t>& sizes, size_t size) {
  for (const size_t s : sizes)
    if (s == size) return true;
  return false;
}
}  // namespace

int main() {
  {
    // One rule for pages and covers: the largest reduction that still covers the output.
    int option = -1;
    check(chooseJpegScale(356.0f / 1350, false, option) == 2 && option == JPEG_SCALE_HALF, "900x1350 at 356 px");
    check(chooseJpegScale(226.0f / 1350, false, option) == 4 && option == JPEG_SCALE_QUARTER, "900x1350 at 226 px");
    check(chooseJpegScale(0.1f, false, option) == 8 && option == JPEG_SCALE_EIGHTH, "a tenth");
    check(chooseJpegScale(0.51f, false, option) == 1 && option == 0, "just over a half");
    check(chooseJpegScale(0.9f, true, option) == 8 && option == JPEG_SCALE_EIGHTH, "progressive");
  }
  {
    // 1. The sleep screen's 2-bit cover of a 1200 x 1800 JPEG fills 528 x 792: a half grid
    // (600 x 900) still covers it, so its row buffer holds 600 columns, not 1200.
    const auto jpeg = fixture("cover-1200x1800.jpg");
    HalFile file(jpeg);
    Output out;
    arrays.clear();
    tracking = true;
    const bool ok = JpegToBmpConverter::jpegFileToBmpStream(file, out, true, false, false);
    tracking = false;
    check(ok, "2-bit screen cover decodes");
    check(out.bytes.size() > 30 && le32(out.bytes, 18) == 528 && le32(out.bytes, 22) == -792,
          "the screen cover is 528 x 792");
    check(has(arrays, 16 * 600) && !has(arrays, 16 * 1200), "the 2-bit cover decodes at full size");
    size_t largest = 0;
    for (const size_t s : arrays) largest = std::max(largest, s);
    std::printf("2-bit 1200x1800 -> 528x792: largest array %zu bytes\n", largest);
  }
  {
    // A baseline cover narrower than the screen grows by linear interpolation, as a progressive one
    // does: the black / white edge of a 128 x 192 cover (an MCU boundary, so no ringing) spreads over
    // a few output columns as gray. Repeating source pixels keeps it a step, only levels 0 and 3.
    const auto jpeg = fixture("edge-128x192.jpg");
    HalFile file(jpeg);
    Output out;
    check(JpegToBmpConverter::jpegFileToBmpStream(file, out, true, false, false), "2-bit upscale decodes");
    const int w = out.bytes.size() > 30 ? le32(out.bytes, 18) : 0, h = out.bytes.size() > 30 ? -le32(out.bytes, 22) : 0;
    const int stride = (w * 2 + 31) / 32 * 4;
    check(w == 528 && h == 792 && out.bytes.size() == 70 + static_cast<size_t>(stride) * h, "the upscaled cover is 528 x 792");
    int minMid = 1 << 30, outside = 0;
    for (int y = 0; w == 528 && y < h; y++) {
      int mid = 0;
      for (int x = 0; x < w; x++) {
        const int level = (out.bytes[70 + y * stride + x / 4] >> (6 - x % 4 * 2)) & 3;
        if (level == 1 || level == 2) {
          mid++;
          if (x < 255 || x > 275) outside++;
        }
      }
      minMid = std::min(minMid, mid);
    }
    std::printf("2-bit 128x192 -> 528x792: gray pixels per row at least %d, outside the edge %d\n", minMid, outside);
    check(minMid >= 1, "an upscaled edge has no gray transition");
    check(outside == 0, "gray pixels away from the edge");
  }
  for (const char* name : {"cover-900x1350.jpg", "cover-1200x1800.jpg"}) {
    // 2. Both thumbnails from one decode.
    const auto jpeg = fixture(name);
    const auto card = fileThumb(jpeg, 356), theme = fileThumb(jpeg, 226);
    check(!card.empty() && !theme.empty(), "the per-height decodes");
    GrayThumb big(356), small(226);
    big.alsoFeed(&small);
    HalFile file(jpeg);
    int scale = 0;
    arrays.clear();
    tracking = true;
    const size_t liveBefore = live;
    peak = live;
    const bool ok = JpegToBmpConverter::jpegFileToGrayThumb(file, big, &scale);
    tracking = false;
    // Home writes a missing thumbnail with 72 KB free (HomeActivity.cpp): the decode's peak is the
    // decoder (17.884 B on the X3, more here with 64-bit pointers) and both thumbnails.
    const size_t decodePeak = peak - liveBefore;
    std::printf("%s: decode peak %zu bytes (JPEGDEC %zu here)\n", name, decodePeak, sizeof(JPEGDEC));
    check(decodePeak - sizeof(JPEGDEC) <= 24 * 1024, "the thumbnails of one decode take more than 24 KB");
    check(ok && big.ready() && small.ready(), "one decode gives both thumbnails");
    Output one, two, scaled;
    check(big.writeTo(one) && one.bytes == card, "the card's thumbnail differs from its own decode");
    check(small.writeTo(two), "the theme's thumbnail");
    big.writeScaled(226, scaled);
    const Gray cover = fullDecode(jpeg);
    const double before = seenError(theme, cover), after = seenError(two.bytes, cover),
                 twice = seenError(scaled.bytes, cover);
    size_t diff = 0;
    const Gray a = fromBmp(theme), b = fromBmp(two.bytes);
    const bool sameSize = a.w == b.w && a.h == b.h;
    for (size_t i = 0; sameSize && i < a.px.size(); i++) diff += a.px[i] != b.px[i];
    std::printf("%s: scale 1/%d, card %s; theme %dx%d vs %dx%d, %zu of %zu pixels differ, seen error "
                "own decode %.2f, one decode %.2f, scaled from the card %.2f\n",
                name, scale, one.bytes == card ? "identical" : "DIFFERS", b.w, b.h, a.w, a.h, diff, a.px.size(),
                before, after, twice);
    check(sameSize, "the theme's thumbnail changed size");
    // Same grid for both heights (1200 x 1800: a quarter for each): the same pixels.
    if (std::string(name) == "cover-1200x1800.jpg") check(two.bytes == theme, "same grid, other pixels");
    // Another grid (900 x 1350: a half for the card, a quarter for the theme): from finer gray, no
    // further from the cover than its own decode, and nearer than dithering it twice.
    check(after <= before * 1.05 + 0.2, "the theme's thumbnail moved away from its cover");
    check(after < twice, "the theme's thumbnail is no nearer its cover than the twice dithered one");
    check(scale == (std::string(name) == "cover-900x1350.jpg" ? 2 : 4), "decode grid");
  }
  {
    // A cover smaller than the card is declined: the reader's per-height decode scales it up.
    const auto jpeg = fixture("cover-180x270.jpg");
    GrayThumb big(356);
    HalFile file(jpeg);
    int scale = 0;
    check(!JpegToBmpConverter::jpegFileToGrayThumb(file, big, &scale) && !big.ready(), "a small cover scaled up");
    check(!fileThumb(jpeg, 356).empty(), "the per-height decode of a small cover");
  }
  {
    // Too little heap: declined before anything is decoded.
    const auto jpeg = fixture("cover-900x1350.jpg");
    GrayThumb big(356);
    HalFile file(jpeg);
    int scale = 0;
    ESP.freeHeap = 40000;
    check(!JpegToBmpConverter::jpegFileToGrayThumb(file, big, &scale), "decoded with the heap gone");
    check(file.bytesRead < 4096, "read the cover with the heap gone");
    ESP.freeHeap = 200000;
  }
  std::printf(failures ? "%d FAILED\n" : "ALL PASS\n", failures);
  return failures ? 1 : 0;
}
