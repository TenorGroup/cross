#include <BuildScratch.h>
#include <HalDisplay.h>
#include <HalMemory.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "BitmapHelpers.h"
#include "MinizConfig.h"

namespace {
std::array<void*, 32> arrayAllocations{};
std::array<void*, 32> rawAllocations{};
// Upstream #2332 moved the scanline, row and gray buffers from malloc to new[],
// and both scaler row buffers are now uint32_t, so a byte size no longer names one
// allocation. The test fails the Nth new[] call instead, for every N the decode makes.
constexpr size_t NO_FAILURE = static_cast<size_t>(-1);
size_t arrayCalls = 0;
size_t failArrayCall = NO_FAILURE;
std::array<size_t, 32> arraySizes{};
bool failureInjected = false;

void record(std::array<void*, 32>& slots, void* ptr) {
  if (!ptr) return;
  for (auto& slot : slots) {
    if (!slot) {
      slot = ptr;
      return;
    }
  }
  std::abort();
}

void forget(std::array<void*, 32>& slots, void* ptr) {
  for (auto& slot : slots) {
    if (slot == ptr) slot = nullptr;
  }
}

void* allocateArray(size_t size) {
  const size_t call = arrayCalls++;
  if (call < arraySizes.size()) arraySizes[call] = size;
  if (call == failArrayCall) {
    failureInjected = true;
    return nullptr;
  }
  void* ptr = std::malloc(size);
  record(arrayAllocations, ptr);
  return ptr;
}

void* testMalloc(size_t size) {
  void* ptr = std::malloc(size);
  record(rawAllocations, ptr);
  return ptr;
}
void* testCalloc(size_t count, size_t size) {
  void* ptr = std::calloc(count, size);
  record(rawAllocations, ptr);
  return ptr;
}
void testFree(void* ptr) {
  forget(rawAllocations, ptr);
  std::free(ptr);
}
}  // namespace

// These arrays belong only to the converter and its real ditherer. The test
// harness uses fixed storage, so any one of them can fail precisely.
void* operator new[](size_t size) {
  if (void* ptr = allocateArray(size)) return ptr;
  throw std::bad_alloc();
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return allocateArray(size); }
void operator delete[](void* ptr) noexcept {
  forget(arrayAllocations, ptr);
  std::free(ptr);
}
void operator delete[](void* ptr, const std::nothrow_t&) noexcept { operator delete[](ptr); }
void operator delete[](void* ptr, size_t) noexcept { operator delete[](ptr); }

// Track the real PNG converter and inflater buffers without intercepting libc
// or the test harness. Miniz and the pixel/dither algorithms are production code.
#define malloc testMalloc
#define calloc testCalloc
#define free testFree
#include "../../lib/miniz/src/InflateStream.cpp"
#include "../../lib/PngToBmpConverter/PngToBmpConverter.cpp"
#undef free
#undef calloc
#undef malloc

namespace {
// Valid grayscale PNG, 6x4, each row [0, 0, 255, 255, 0, 255].
constexpr uint8_t PNG[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x88, 0x6f, 0x11, 0x9f,
    0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x60, 0xf8, 0xff, 0x9f, 0xe1,
    0x3f, 0x03, 0x36, 0x0a, 0x00, 0x9d, 0x7e, 0x0b, 0xf5, 0x3b, 0x3d, 0xab, 0xd1, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

class Output : public Print {
 public:
  std::array<uint8_t, 128> bytes{};
  size_t size = 0;
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t count) override {
    if (count > bytes.size() - size) return 0;
    std::memcpy(bytes.data() + size, data, count);
    size += count;
    return count;
  }
};

bool decode(Output& out) {
  HalFile input(PNG, sizeof(PNG));
  return PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(input, out, 3, 2);
}

bool noLeaks() {
  for (void* ptr : arrayAllocations) if (ptr) return false;
  for (void* ptr : rawAllocations) if (ptr) return false;
  return true;
}
}  // namespace

int main() {
  Output baseline;
  arrayCalls = 0;
  if (!decode(baseline) || !noLeaks() || baseline.size != 70 || baseline.bytes[0] != 'B' ||
      baseline.bytes[1] != 'M' || baseline.bytes[18] != 3 || baseline.bytes[28] != 1 ||
      baseline.bytes[62] != 0x40) {
    std::puts("FAIL: real PNG did not produce the expected 3x2 monochrome BMP");
    return 1;
  }

  const size_t calls = arrayCalls;
  const std::array<size_t, 32> baselineSizes = arraySizes;
  // The two scaler row buffers (accumulator and count, 3 output pixels each) must be among them.
  size_t scalerBuffers = 0;
  for (size_t i = 0; i < calls && i < arraySizes.size(); ++i) scalerBuffers += baselineSizes[i] == 3 * sizeof(uint32_t);
  if (calls > arraySizes.size() || scalerBuffers < 2) {
    std::printf("FAIL: decode made %zu new[] calls, %zu of them scaler-sized; expected both scaler buffers\n", calls,
                scalerBuffers);
    return 1;
  }

  for (size_t call = 0; call < calls; ++call) {
    const size_t size = baselineSizes[call];
    arrayCalls = 0;
    failArrayCall = call;
    failureInjected = false;
    Output failed;
    bool returned = false;
    try {
      returned = decode(failed);
    } catch (const std::bad_alloc&) {
      std::printf("FAIL: new[] call %zu (%zu bytes) escaped as bad_alloc (firmware abort)\n", call, size);
      return 1;
    }
    failArrayCall = NO_FAILURE;
    if (!failureInjected || returned || !noLeaks()) {
      std::printf("FAIL: new[] call %zu (%zu bytes) was not rejected cleanly\n", call, size);
      return 1;
    }
    Output retry;
    if (!decode(retry) || !noLeaks() || retry.size != baseline.size || retry.bytes != baseline.bytes) {
      std::printf("FAIL: decode after new[] call %zu (%zu bytes) failed did not recover\n", call, size);
      return 1;
    }
    std::printf("PASS: new[] call %zu (%zu bytes) failed, decode returned false, freed every buffer, retry BMP unchanged\n",
                call, size);
  }
  return 0;
}
