#include <Arduino.h>
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
#include <set>
#include <string>

#include "BitmapHelpers.h"
#include "MinizConfig.h"
#include "../../lib/JpegToBmpConverter/TenorInk.h"

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
std::set<std::string> doors;
const char* refuseWho = nullptr;
bool refused = false;
}  // namespace

EspClass ESP;
// The heap floor door (built as on the device, ESP_PLATFORM): records each name, refuses one on demand.
namespace heapfloor {
bool judge(size_t bytes, const void*, const char* who) {
  doors.insert(who);
  if (refuseWho && std::strcmp(who, refuseWho) == 0) { refused = true; return false; }
  return stands(bytes, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}
void* take(size_t bytes, const char* who) {
  void* p = testMalloc(bytes);
  if (p && !judge(bytes, p, who)) { testFree(p); return nullptr; }
  return p;
}
}  // namespace heapfloor

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
#define writeBmpHeader1bit pngHeader1bit
#define writeBmpHeader2bit pngHeader2bit
#include "../../lib/PngToBmpConverter/PngToBmpConverter.cpp"
#undef writeBmpHeader1bit
#undef writeBmpHeader2bit
#undef free
#undef calloc
#undef malloc

void writeBmpHeader1bit(Print& out, int w, int h) { pngHeader1bit(out, w, h); }
void writeBmpHeader2bit(Print& out, int w, int h) { pngHeader2bit(out, w, h); }

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
  Output exact;
  HalFile exactInput(PNG, sizeof(PNG));
  if (!PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(exactInput, exact, 4, 3, true) ||
      exact.bytes[18] != 4 || exact.bytes[22] != 0xfd || exact.size != 74 || !noLeaks()) {
    std::puts("FAIL: exact Home PNG dimensions or allocation cleanup");
    return 1;
  }
  std::puts("PASS: exact 4x3 Home PNG dimensions");
  std::array<uint8_t, 24> gray{};
  for (size_t y = 0; y < 4; ++y)
    for (size_t x = 0; x < 6; ++x) gray[y * 6 + x] = (x == 2 || x == 3 || x == 5) ? 255 : 0;
  for (bool four : {true, false}) {
    Output expected, streamed;
    arrayCalls = 0;
    HalFile input(PNG, sizeof(PNG));
    if (!PngToBmpConverter::pngFileToInkBmpStream(input, streamed, 4, 3, four) || !noLeaks()) {
      std::puts("FAIL: tenor/ink PNG streaming or allocation cleanup");
      return 1;
    }
    const size_t inkCalls = arrayCalls;
    if (!tenorink::writeCover(gray.data(), 6, 4, expected, 4, 3, four) ||
        expected.size != streamed.size || expected.bytes != streamed.bytes || !noLeaks()) {
      std::puts("FAIL: tenor/ink PNG differs from the shared array kernel");
      return 1;
    }
    for (size_t call = 0; call < inkCalls; ++call) {
      arrayCalls = 0;
      failArrayCall = call;
      failureInjected = false;
      HalFile retry(PNG, sizeof(PNG));
      Output rejected;
      const bool ok = PngToBmpConverter::pngFileToInkBmpStream(retry, rejected, 4, 3, four);
      failArrayCall = NO_FAILURE;
      if (!failureInjected || ok || !noLeaks()) {
        std::puts("FAIL: tenor/ink PNG allocation failure did not unwind cleanly");
        return 1;
      }
    }
    std::printf("PASS: tenor/ink PNG levels=%d, shared kernel parity, %zu clean allocation failures\n",
                four ? 4 : 1, inkCalls);
    // The X3 tenor/ink buffers each pass the heap floor door under their own name, and a refusal unwinds.
    doors.clear();
    HalFile named(PNG, sizeof(PNG));
    Output seen;
    if (!PngToBmpConverter::pngFileToInkBmpStream(named, seen, 4, 3, four) || !noLeaks()) return 1;
    for (const char* who : {"ink-png", "ink-png-row", "ink-bounds", "ink-rows", "ink-errors", "inflate-state",
                            "inflate-window"}) {
      if (!doors.count(who)) {
        std::printf("FAIL: tenor/ink PNG block %s did not pass the heap floor door\n", who);
        return 1;
      }
      refuseWho = who;
      refused = false;
      HalFile retry(PNG, sizeof(PNG));
      Output rejected;
      const bool ok = PngToBmpConverter::pngFileToInkBmpStream(retry, rejected, 4, 3, four);
      refuseWho = nullptr;
      if (!refused || ok || !noLeaks()) {
        std::printf("FAIL: heap floor refusal of %s did not unwind cleanly\n", who);
        return 1;
      }
    }
    std::printf("PASS: tenor/ink PNG levels=%d, 7 heap floor names refused cleanly\n", four ? 4 : 1);
  }
  return 0;
}
