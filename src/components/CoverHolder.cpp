#include "CoverHolder.h"

#include <HeapFloor.h>
#include <InflateStream.h>
#include <TenorInk.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>

#include "CoverHolderMaster.h"

namespace coverholder {

std::string dir() {
  char name[48];
  snprintf(name, sizeof(name), "/.crosspoint/cover-holder-%08x", static_cast<unsigned>(MASTER_ID));
  return name;
}

std::string path() { return dir() + "/thumb_[HEIGHT].bmp"; }

static_assert(STRIP_ROWS * WIDTH < 4096, "a strip buffer stays under the heap floor door");

bool decode(const Sink sink, void* ctx) {
  auto strip = heapfloor::array<uint8_t[]>(static_cast<size_t>(STRIP_ROWS) * WIDTH, "holder-strip");
  if (!strip || !sink) return false;
  InflateStream inflate;
  for (int i = 0; i < STRIPS; ++i) {
    const int y = i * STRIP_ROWS;
    const int rows = HEIGHT - y < STRIP_ROWS ? HEIGHT - y : STRIP_ROWS;
    const uint32_t from = i == 0 ? 0 : STRIP_END[i - 1];
    // A one-shot stream: the strip's own buffer is the dictionary, so no 32 KB window is taken.
    if (!inflate.init(false)) return false;
    inflate.setZlibWrapped();
    inflate.setSource(STRIPS_DATA + from, STRIP_END[i] - from);
    if (!inflate.read(strip.get(), static_cast<size_t>(rows) * WIDTH)) return false;
    if (!sink(ctx, y, rows, strip.get())) return false;
    if ((i & 7) == 7) vTaskDelay(1);
  }
  return true;
}

bool writeInk(Print& out, const int w, const int h, const bool four) {
  tenorink::CoverStream stream(WIDTH, HEIGHT, out, w, h, four);
  if (!stream.valid()) return false;
  const bool whole = decode(
      [](void* ctx, const int y, const int rows, const uint8_t* rowsData) {
        auto& stream = *static_cast<tenorink::CoverStream*>(ctx);
        for (int r = 0; r < rows; ++r)
          if (!stream.push(rowsData + static_cast<size_t>(r) * WIDTH, y + r)) return false;
        return true;
      },
      &stream);
  return whole && stream.complete();
}

}  // namespace coverholder
