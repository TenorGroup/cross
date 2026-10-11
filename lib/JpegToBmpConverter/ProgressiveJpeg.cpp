#include "ProgressiveJpeg.h"

#ifdef BOARD_HAS_PSRAM
#include <HalStorage.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstring>

namespace {
void* jpegAlloc(size_t bytes) {
  const auto heap = HalMemory::getPsramHeap();
  if (heap.freeBytes < 128 * 1024 || bytes > heap.freeBytes - 128 * 1024 || bytes > heap.largestBlockBytes)
    return nullptr;
  return HalMemory::allocatePsram(bytes).release();
}
// Every scan of a progressive JPEG walks all the blocks of the image. Cameras and exporters write 10 to 20 scans
// (libjpeg's default script has 10); a file with more than kMaxScans is refused as hostile, and the cover falls
// back to the reduced JPEGDEC path as for any file this decoder cannot take.
constexpr unsigned kMaxScans = 64;
void jpegFree(void* ptr) { HalMemory::PsramDeleter{}(static_cast<uint8_t*>(ptr)); }
void* jpegRealloc(void* ptr, size_t oldSize, size_t newSize) {
  void* next = jpegAlloc(newSize);
  if (next) {
    if (ptr) std::memcpy(next, ptr, std::min(oldSize, newSize));
    jpegFree(ptr);
  }
  return next;
}
}  // namespace

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 3072
#define STBI_MALLOC(bytes) jpegAlloc(bytes)
#define STBI_REALLOC_SIZED(ptr, oldSize, newSize) jpegRealloc(ptr, oldSize, newSize)
#define STBI_FREE(ptr) jpegFree(ptr)
#include "vendor/stb_image.h"

namespace progressivejpeg {
bool supported() { return HalMemory::getPsramHeap().totalBytes > 0; }
HalMemory::PsramBuffer allocate(size_t bytes) {
  return HalMemory::PsramBuffer(static_cast<uint8_t*>(jpegAlloc(bytes)));
}

HalMemory::PsramBuffer decode(HalFile& file, int width, int height) {
  if (!supported() || width <= 0 || height <= 0 || width > 2048 || height > 3072 || !file.seek(0)) return {};
  // scans counts the SOS markers (FF DA) the decoder has read; past kMaxScans the file ends for it.
  struct Reader { HalFile& file; size_t pos = 0; unsigned reads = 0; unsigned scans = 0; uint8_t last = 0; } reader{file};
  const stbi_io_callbacks io{
    [](void* user, char* data, int count) -> int {
      auto& r = *static_cast<Reader*>(user);
      if (r.scans > kMaxScans) return 0;
      const int n = r.file.read(data, count);
      if (n > 0) {
        r.pos += n;
        for (int i = 0; i < n; ++i) {
          const auto b = static_cast<uint8_t>(data[i]);
          if (r.last == 0xFF && b == 0xDA) ++r.scans;
          r.last = b;
        }
      }
      if ((++r.reads & 15) == 0) vTaskDelay(1);
      return std::max(n, 0);
    },
    [](void* user, int count) {
      auto& r = *static_cast<Reader*>(user);
      const int64_t next = static_cast<int64_t>(r.pos) + count;
      if (next >= 0 && static_cast<uint64_t>(next) <= r.file.size() && r.file.seek(next)) r.pos = next;
      r.last = 0;
    },
    [](void* user) -> int { auto& r = *static_cast<Reader*>(user); return r.scans > kMaxScans || r.pos >= r.file.size(); }
  };
  int w = 0, h = 0, channels = 0;
  // Check the decoder's own dimensions before it allocates image planes or coefficients.
  if (!stbi_info_from_callbacks(&io, &reader, &w, &h, &channels) || w != width || h != height || !file.seek(0))
    return {};
  reader.pos = 0;
  reader.scans = 0;
  reader.last = 0;
  HalMemory::PsramBuffer pixels(stbi_load_from_callbacks(&io, &reader, &w, &h, &channels, 1));
  // The decoder keeps what it decoded of a file that ends early, so the cut is read from the count.
  if (reader.scans > kMaxScans || w != width || h != height) return {};
  return pixels;
}
}  // namespace progressivejpeg
#endif
