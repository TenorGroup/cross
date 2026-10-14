#pragma once

#include <HeapFloor.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// A rect of the screen buffer held, saved and put back in strips of 4 KB, never as one block as big as the rect. The X3 after a
// Bluetooth session has 90 KB of free heap and a largest block of 28.660 B; a Shelf or Large cover frame is 39.840 to
// 43.296 B. Every strip is one allocation through the heap floor's door; one refused, the frame is simply not kept.
namespace framestrip {

constexpr size_t STRIP_BYTES = 4096;

// Whole rows of the region, one strip at a time: `visit(firstRow, rows, bytes)`, in order.
template <class Renderer, class Visit>
bool strips(const Renderer& r, const int x, const int y, const int w, const int h, Visit&& visit) {
  int perRow = 0, rowCount = 0;
  if (!r.getRegionRows(x, y, w, h, perRow, rowCount) || perRow <= 0 || perRow > static_cast<int>(STRIP_BYTES)) return false;
  const int stripRows = static_cast<int>(STRIP_BYTES) / perRow;
  for (int first = 0; first < rowCount; first += stripRows) {
    const int rows = std::min(stripRows, rowCount - first);
    if (!visit(first, rows, static_cast<size_t>(rows) * perRow)) return false;
  }
  return true;
}

// The region as the screen buffer held it at one moment, in blocks of 4 KB from the door. Held from the frame's build to the
// write of its file, then released: the screen buffer changes in between (the chevrons of the footer, a notice).
struct Snapshot {
  static constexpr int MAX_STRIPS = 16;  // 65.536 B, more than the widest panel without PSRAM
  uint8_t* strip[MAX_STRIPS] = {};
  size_t bytes[MAX_STRIPS] = {};
  int count = 0;
  size_t total = 0;
  Snapshot() = default;
  Snapshot(const Snapshot&) = delete;
  Snapshot& operator=(const Snapshot&) = delete;
  ~Snapshot() { release(); }
  void release() {
    while (count > 0) free(strip[--count]);
    total = 0;
  }
  explicit operator bool() const { return count > 0; }
};

// Takes the region into `out`. False, and nothing held, when the door refuses a strip or the region is too big.
template <class Renderer>
bool capture(const Renderer& r, const int x, const int y, const int w, const int h, Snapshot& out) {
  out.release();
  const bool ok = strips(r, x, y, w, h, [&](const int first, const int rows, const size_t bytes) {
    if (out.count == Snapshot::MAX_STRIPS) return false;
    auto* block = static_cast<uint8_t*>(heapfloor::take(STRIP_BYTES, "home-strip"));
    if (!block) return false;
    out.strip[out.count] = block;
    out.bytes[out.count++] = bytes;
    out.total += bytes;
    return r.copyRegionRowsToBuffer(x, y, w, h, first, rows, block, STRIP_BYTES);
  });
  if (!ok) out.release();
  return ok;
}

// source(uint8_t* into, size_t count) fills each strip in order (a file read); the strip then goes to the screen buffer, from one
// block of 4 KB that is given back at once.
template <class Renderer, class Source>
bool fill(const Renderer& r, const int x, const int y, const int w, const int h, Source&& source) {
  auto* block = static_cast<uint8_t*>(heapfloor::take(STRIP_BYTES, "home-strip"));
  if (!block) return false;
  const bool ok = strips(r, x, y, w, h, [&](const int first, const int rows, const size_t bytes) {
    return source(block, bytes) && r.copyBufferToRegionRows(x, y, w, h, first, rows, block, STRIP_BYTES);
  });
  free(block);
  return ok;
}

}  // namespace framestrip
