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

// FNV-1a of a BMP: the packing code (bit index, mask, shift) must move no byte.
uint64_t fnv(const std::vector<uint8_t>& b) {
  uint64_t h = 1469598103934665603ULL;
  for (const uint8_t c : b) h = (h ^ c) * 1099511628211ULL;
  return h;
}

struct Golden {
  const char* key;
  uint64_t hash;
};
// Hashes of the BMPs each output path writes today: widths of every residue modulo 8 (528, 203, 97,
// 101, 227, 150 ...), 1-bit and 2-bit packing, thumbnails, upscale and downscale.
constexpr Golden kGolden[] = {
    {"edge-128x192.jpg screen 2-bit fill", 0xa18e09906521de99ULL},
    {"edge-128x192.jpg screen 2-bit fit", 0xa18e09906521de99ULL},
    {"edge-128x192.jpg screen 2-bit original", 0xe9657f2388775f25ULL},
    {"edge-128x192.jpg screen 1-bit", 0x2cb787efaf179e9bULL},
    {"edge-128x192.jpg 2-bit 203x301", 0x685d8815f07726d3ULL},
    {"edge-128x192.jpg 2-bit 97x130", 0x55e57ca42765b289ULL},
    {"edge-128x192.jpg 1-bit 101x151", 0x602da45427a3663aULL},
    {"edge-128x192.jpg 1-bit 227x340", 0x342e86cb0c1a7a54ULL},
    {"edge-128x192.jpg thumb 356", 0xf6f673c2c752b132ULL},
    {"edge-128x192.jpg thumb 226", 0x45957461d9503960ULL},
    {"edge-128x192.jpg thumb 301", 0x8cba3d2750c3f544ULL},
    {"edge-128x192.jpg thumb 151", 0xce12700b657cc59fULL},
    {"edge-128x192.jpg thumb 151 card", 0xce12700b657cc59fULL},
    {"edge-128x192.jpg thumb 151 theme", 0x5dddac61badbbc0eULL},
    {"edge-128x192.jpg thumb 151 scaled", 0x0d59f09c5a318a8eULL},
    {"cover-180x270.jpg screen 2-bit fill", 0xda9cd2ea5e73d977ULL},
    {"cover-180x270.jpg screen 2-bit fit", 0xda9cd2ea5e73d977ULL},
    {"cover-180x270.jpg screen 2-bit original", 0x86d068d90347fb69ULL},
    {"cover-180x270.jpg screen 1-bit", 0x3d4f47d782a8a8e8ULL},
    {"cover-180x270.jpg 2-bit 203x301", 0x64ae95e77f74941cULL},
    {"cover-180x270.jpg 2-bit 97x130", 0x1e9b89471c2d749eULL},
    {"cover-180x270.jpg 1-bit 101x151", 0x9f42bbe4bd549cfaULL},
    {"cover-180x270.jpg 1-bit 227x340", 0x8d67981491caeb7dULL},
    {"cover-180x270.jpg thumb 356", 0x0ba6bcd823283351ULL},
    {"cover-180x270.jpg thumb 226", 0xef19aab49cdf3b3fULL},
    {"cover-180x270.jpg thumb 226 card", 0xef19aab49cdf3b3fULL},
    {"cover-180x270.jpg thumb 226 theme", 0x6f344254839e6de6ULL},
    {"cover-180x270.jpg thumb 226 scaled", 0xcdcfd6dc182365b4ULL},
    {"cover-180x270.jpg thumb 301", 0x83ad86dddcd7fd36ULL},
    {"cover-180x270.jpg thumb 151", 0x3f8dc8c6138d9b89ULL},
    {"cover-180x270.jpg thumb 151 card", 0x3f8dc8c6138d9b89ULL},
    {"cover-180x270.jpg thumb 151 theme", 0xf9d0d9faf8106b56ULL},
    {"cover-180x270.jpg thumb 151 scaled", 0x838051e97eb18000ULL},
    {"cover-900x1350.jpg screen 2-bit fill", 0x426fd1f62a0cb728ULL},
    {"cover-900x1350.jpg screen 2-bit fit", 0x426fd1f62a0cb728ULL},
    {"cover-900x1350.jpg screen 2-bit original", 0x2d587304a4b18489ULL},
    {"cover-900x1350.jpg screen 1-bit", 0xd929544b46d5a5fcULL},
    {"cover-900x1350.jpg 2-bit 203x301", 0xab065454d591dec8ULL},
    {"cover-900x1350.jpg 2-bit 97x130", 0xbba73ee114589e07ULL},
    {"cover-900x1350.jpg 1-bit 101x151", 0xf4a8a410d894e76fULL},
    {"cover-900x1350.jpg 1-bit 227x340", 0xe41ee49a1dd791e2ULL},
    {"cover-900x1350.jpg thumb 356", 0x29e8443a6fc62facULL},
    {"cover-900x1350.jpg thumb 356 card", 0x29e8443a6fc62facULL},
    {"cover-900x1350.jpg thumb 356 theme", 0x48dddb03ff0d949aULL},
    {"cover-900x1350.jpg thumb 356 scaled", 0x2af6af67a2ecbb16ULL},
    {"cover-900x1350.jpg thumb 226", 0x2d7099016a453283ULL},
    {"cover-900x1350.jpg thumb 226 card", 0x2d7099016a453283ULL},
    {"cover-900x1350.jpg thumb 226 theme", 0x63ffb7b7a563e3a5ULL},
    {"cover-900x1350.jpg thumb 226 scaled", 0x376d2a8bc352960eULL},
    {"cover-900x1350.jpg thumb 301", 0x9000b53b4f23a09dULL},
    {"cover-900x1350.jpg thumb 301 card", 0x9000b53b4f23a09dULL},
    {"cover-900x1350.jpg thumb 301 theme", 0xd59b6398ccc17663ULL},
    {"cover-900x1350.jpg thumb 301 scaled", 0x5769f7fccb45400bULL},
    {"cover-900x1350.jpg thumb 151", 0xb61723a33e657f1bULL},
    {"cover-900x1350.jpg thumb 151 card", 0xb61723a33e657f1bULL},
    {"cover-900x1350.jpg thumb 151 theme", 0x6ac109fb8e604c98ULL},
    {"cover-900x1350.jpg thumb 151 scaled", 0x4c2cbebd9635157aULL},
    {"cover-1200x1800.jpg screen 2-bit fill", 0xf88eedc762a9141aULL},
    {"cover-1200x1800.jpg screen 2-bit fit", 0xf88eedc762a9141aULL},
    {"cover-1200x1800.jpg screen 2-bit original", 0x96c8e460e3ef4ba2ULL},
    {"cover-1200x1800.jpg screen 1-bit", 0x7f56e2fbbaf32da3ULL},
    {"cover-1200x1800.jpg 2-bit 203x301", 0xc100ceeaf674b16dULL},
    {"cover-1200x1800.jpg 2-bit 97x130", 0x0c75ec1093f6aa69ULL},
    {"cover-1200x1800.jpg 1-bit 101x151", 0x2c302e0a719ff93fULL},
    {"cover-1200x1800.jpg 1-bit 227x340", 0xebe58da23d5a69a4ULL},
    {"cover-1200x1800.jpg thumb 356", 0xb18c3bcd1a75ecffULL},
    {"cover-1200x1800.jpg thumb 356 card", 0xb18c3bcd1a75ecffULL},
    {"cover-1200x1800.jpg thumb 356 theme", 0xcc446c10c049462bULL},
    {"cover-1200x1800.jpg thumb 356 scaled", 0xfa07b66220dc8051ULL},
    {"cover-1200x1800.jpg thumb 226", 0x1b362742abe499e9ULL},
    {"cover-1200x1800.jpg thumb 226 card", 0x1b362742abe499e9ULL},
    {"cover-1200x1800.jpg thumb 226 theme", 0x01e093a16ecfd9e5ULL},
    {"cover-1200x1800.jpg thumb 226 scaled", 0x7a980b1eafbd3bb2ULL},
    {"cover-1200x1800.jpg thumb 301", 0x2aece473a109744fULL},
    {"cover-1200x1800.jpg thumb 301 card", 0x2aece473a109744fULL},
    {"cover-1200x1800.jpg thumb 301 theme", 0x803ea79daaab4506ULL},
    {"cover-1200x1800.jpg thumb 301 scaled", 0x82dcd18cdc3ea039ULL},
    {"cover-1200x1800.jpg thumb 151", 0x77713d254c93720cULL},
    {"cover-1200x1800.jpg thumb 151 card", 0x77713d254c93720cULL},
    {"cover-1200x1800.jpg thumb 151 theme", 0x2e550bcf5cdc525dULL},
    {"cover-1200x1800.jpg thumb 151 scaled", 0xfc89c540dc3acf39ULL},
};

void checkGolden(const std::string& key, const std::vector<uint8_t>& bytes) {
  const uint64_t got = fnv(bytes);
  for (const Golden& g : kGolden) {
    if (key != g.key) continue;
    check(g.hash == got, key.c_str());
    if (g.hash != got)
      std::printf("GOLD got {\"%s\", 0x%016llxULL},\n", key.c_str(), static_cast<unsigned long long>(got));
    return;
  }
  std::printf("GOLD new {\"%s\", 0x%016llxULL},\n", key.c_str(), static_cast<unsigned long long>(got));
  check(false, ("no golden for " + key).c_str());
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
    check(w == 528 && h == 792 && out.bytes.size() == 70 + static_cast<size_t>(stride) * h,
          "the upscaled cover is 528 x 792");
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
  for (const char* name : {"edge-128x192.jpg", "cover-180x270.jpg", "cover-900x1350.jpg", "cover-1200x1800.jpg"}) {
    // 3. Byte for byte: every converter output, so a change to how a row is packed shows at once.
    const auto jpeg = fixture(name);
    const std::string n = name;
    using Conv = JpegToBmpConverter;
    const auto bmp = [&](const char* what, auto&& convert) {
      HalFile file(jpeg);
      Output out;
      check(convert(file, out), what);
      checkGolden(n + " " + what, out.bytes);
    };
    bmp("screen 2-bit fill",
        [](HalFile& f, Output& o) { return Conv::jpegFileToBmpStream(f, o, true, false, false); });
    bmp("screen 2-bit fit",
        [](HalFile& f, Output& o) { return Conv::jpegFileToBmpStream(f, o, false, false, false); });
    bmp("screen 2-bit original",
        [](HalFile& f, Output& o) { return Conv::jpegFileToBmpStream(f, o, true, true, false); });
    bmp("screen 1-bit",
        [](HalFile& f, Output& o) { return Conv::jpegFileToBmpStream(f, o, true, false, true); });
    bmp("2-bit 203x301",
        [](HalFile& f, Output& o) { return Conv::jpegFileToBmpStreamWithSize(f, o, 203, 301); });
    bmp("2-bit 97x130",
        [](HalFile& f, Output& o) { return Conv::jpegFileToBmpStreamWithSize(f, o, 97, 130); });
    bmp("1-bit 101x151",
        [](HalFile& f, Output& o) { return Conv::jpegFileTo1BitBmpStreamWithSize(f, o, 101, 151); });
    bmp("1-bit 227x340",
        [](HalFile& f, Output& o) { return Conv::jpegFileTo1BitBmpStreamWithSize(f, o, 227, 340); });
    for (const int height : {356, 226, 301, 151}) {
      const std::string key = n + " thumb " + std::to_string(height);
      checkGolden(key, fileThumb(jpeg, height));
      GrayThumb big(height), small(height * 2 / 3);
      big.alsoFeed(&small);
      HalFile file(jpeg);
      int scale = 0;
      if (!JpegToBmpConverter::jpegFileToGrayThumb(file, big, &scale)) continue;
      Output card, theme, scaled;
      check(big.writeTo(card) && small.writeTo(theme) && big.writeScaled(height * 2 / 3, scaled), "gray thumbs write");
      checkGolden(key + " card", card.bytes);
      checkGolden(key + " theme", theme.bytes);
      checkGolden(key + " scaled", scaled.bytes);
    }
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
