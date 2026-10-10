// Host tool and checks for the cover placeholder (src/components/CoverHolder.cpp).
//   Tool ink <w> <h> <4|1> <out.bmp>      the tenor/ink BMP of the master at w x h, streamed strip by strip
//   Tool decode <master.pgm>              the master inflated from the strips equals the raw master
//   Tool check <master.pgm>               heap peak at the X3 frames and clean failure when the heap refuses
#include <Arduino.h>
#include <Print.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <utility>
#include <vector>

// The allocation counters. malloc inside InflateStream.cpp is routed here (as test/inflate_stream does)
// and so is operator new, so the peak is what the device's heap would carry at once.
namespace {
size_t liveBytes = 0, peakBytes = 0, attempts = 0, failAt = 0;
bool trace = false;
bool counting = false;
struct Header { size_t size; size_t pad; };
void* trackedTake(size_t size) {
  if (counting && ++attempts == failAt) return nullptr;
  auto* h = static_cast<Header*>(std::malloc(sizeof(Header) + size));
  if (!h) return nullptr;
  h->size = size;
  if (counting && trace && attempts <= 5) std::printf("  alloc %zu\n", size);
  if (counting) { liveBytes += size; if (liveBytes > peakBytes) peakBytes = liveBytes; }
  return h + 1;
}
void release(void* p) {
  if (!p) return;
  auto* h = static_cast<Header*>(p) - 1;
  if (counting) liveBytes -= h->size;
  std::free(h);
}
}  // namespace
void* operator new(size_t n) { if (void* p = trackedTake(n)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { if (void* p = trackedTake(n)) return p; throw std::bad_alloc(); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return trackedTake(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return trackedTake(n); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, size_t) noexcept { release(p); }
void operator delete[](void* p, size_t) noexcept { release(p); }

#define malloc trackedTake
#define free release
#include "../../lib/miniz/src/InflateStream.cpp"
#undef free
#undef malloc

#include <GrayThumb.h>
#include <HeapFloor.h>
#include <TenorInk.h>

#include "components/CoverHolder.h"
#include "components/CoverHolderMaster.h"

EspClass ESP;

// writeBmpHeader2bit as JpegToBmpConverter.cpp writes it (that file needs the JPEG decoder to build).
namespace {
void put32(Print& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.write(static_cast<uint8_t>(v >> (8 * i))); }
void put16(Print& o, uint16_t v) { o.write(static_cast<uint8_t>(v)); o.write(static_cast<uint8_t>(v >> 8)); }
}  // namespace
void writeBmpHeader2bit(Print& out, const int width, const int height) {
  const int bytesPerRow = (width * 2 + 31) / 32 * 4;
  out.write('B'); out.write('M');
  put32(out, 70 + bytesPerRow * height); put32(out, 0); put32(out, 70);
  put32(out, 40); put32(out, width); put32(out, static_cast<uint32_t>(-height));
  put16(out, 1); put16(out, 2); put32(out, 0); put32(out, bytesPerRow * height);
  put32(out, 2835); put32(out, 2835); put32(out, 4); put32(out, 4);
  const uint8_t palette[16] = {0, 0, 0, 0, 0x55, 0x55, 0x55, 0, 0xAA, 0xAA, 0xAA, 0, 0xFF, 0xFF, 0xFF, 0};
  for (const uint8_t b : palette) out.write(b);
}

namespace {
class FileOut final : public Print {
 public:
  std::ofstream file;
  size_t bytes = 0;
  explicit FileOut(const char* path) : file(path, std::ios::binary) {}
  size_t write(uint8_t b) override { file.put(static_cast<char>(b)); ++bytes; return 1; }
  size_t write(const uint8_t* d, size_t n) override { file.write(reinterpret_cast<const char*>(d), n); bytes += n; return n; }
};
class NullOut final : public Print {
 public:
  size_t bytes = 0;
  size_t write(uint8_t) override { ++bytes; return 1; }
  size_t write(const uint8_t* , size_t n) override { bytes += n; return n; }
};

std::vector<uint8_t> readPgm(const char* path) {
  std::ifstream in(path, std::ios::binary);
  std::string magic; int w, h, max;
  in >> magic >> w >> h >> max;
  in.get();
  std::vector<uint8_t> px{std::istreambuf_iterator<char>(in), {}};
  if (magic != "P5" || w != coverholder::WIDTH || h != coverholder::HEIGHT || px.size() != size_t(w) * h) std::exit(2);
  return px;
}

int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string cmd = argc > 1 ? argv[1] : "";
  ESP.freeHeap = 90000;  // the X3 at Home, radio on
  if (cmd == "ink" && argc == 6) {
    FileOut out(argv[5]);
    return coverholder::writeInk(out, std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]) == 4) ? 0 : 1;
  }
  if (cmd == "decode" && argc == 3) {
    const auto master = readPgm(argv[2]);
    std::vector<uint8_t> got(master.size());
    const bool ok = coverholder::decode(
        [](void* ctx, int y, int rows, const uint8_t* g) {
          std::memcpy(static_cast<std::vector<uint8_t>*>(ctx)->data() + size_t(y) * coverholder::WIDTH, g,
                      size_t(rows) * coverholder::WIDTH);
          return true;
        },
        &got);
    return ok && got == master ? 0 : 1;
  }
  if (cmd == "check" && argc == 3) {
    const auto master = readPgm(argv[2]);
    {
      char id[16];
      std::snprintf(id, sizeof(id), "%08x", static_cast<unsigned>(coverholder::MASTER_ID));
      check(coverholder::dir() == std::string("/.crosspoint/cover-holder-") + id &&
                coverholder::path() == coverholder::dir() + "/thumb_[HEIGHT].bmp",
            "the cache directory is named after the master");
    }
    // 1. The X3 frames (298x450 and 230x347, 4 levels and 1 bit): the heap held at once, nothing left.
    for (const int four : {1, 0}) {
      for (const auto size : {std::pair<int, int>{298, 450}, {230, 347}}) {
        NullOut out;
        liveBytes = peakBytes = 0; attempts = 0; failAt = 0; counting = true; trace = size.second == 450 && four;
        const bool ok = coverholder::writeInk(out, size.first, size.second, four);
        trace = false; counting = false;
        std::printf("x3 %dx%d levels=%d ok=%d peak=%zu live_after=%zu\n", size.first, size.second, four ? 4 : 1, ok, peakBytes, liveBytes);
        check(ok, "writeInk at an X3 frame");
        check(liveBytes == 0, "writeInk leaves bytes allocated");
        check(peakBytes <= 24 * 1024, "writeInk peak over 24 KB");
      }
    }
    // 2. Every allocation refused in turn (the heap floor door answers nullptr): a clean false, nothing left.
    {
      size_t total = 0;
      { NullOut out; liveBytes = peakBytes = 0; attempts = 0; failAt = 0; counting = true; coverholder::writeInk(out, 298, 450, true); counting = false; total = attempts; }
      std::printf("x3 allocations at 298x450: %zu\n", total);
      check(total >= 6 && total < 200, "allocation count of writeInk");
      for (size_t k = 1; k <= total; ++k) {
        NullOut out;
        liveBytes = 0; attempts = 0; failAt = k; counting = true;
        bool ok = true;
        try { ok = coverholder::writeInk(out, 298, 450, true); } catch (...) { check(false, "writeInk threw"); }
        counting = false;
        if (ok) check(false, "writeInk succeeded with an allocation refused");
        if (liveBytes != 0) { std::printf("leak after refusing allocation %zu: %zu\n", k, liveBytes); check(false, "bytes left after a refusal"); }
      }
    }
    std::printf("%s\n", failures ? "RED" : "GREEN");
    return failures ? 1 : 0;
  }
  return 2;
}
