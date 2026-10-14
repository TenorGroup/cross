#pragma once

#include <HeapFloor.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// A rect of the screen buffer to the card and back in strips of 4 KB, never as one block as big as the rect. The X3 after a
// Bluetooth session has 90 KB of free heap and a largest block of 28.660 B; a Shelf or Large cover frame is 39.840 to
// 43.296 B. The strip is the only block the card step takes, one allocation through the heap floor's door; refused, the
// frame is simply not kept.
namespace framestrip {

constexpr size_t STRIP_BYTES = 4096;

// The strip's memory, from the door. Empty when the heap is under the floor.
struct Buffer {
  uint8_t* data = static_cast<uint8_t*>(heapfloor::take(STRIP_BYTES, "home-strip"));
  Buffer() = default;
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  ~Buffer() { free(data); }
  explicit operator bool() const { return data != nullptr; }
};

// Whole rows of the region in `data`, one strip at a time: `visit(firstRow, rows, bytes)` has the strip in data[0, bytes).
template <class Renderer, class Visit>
bool strips(const Renderer& r, const int x, const int y, const int w, const int h, const Buffer& buffer, Visit&& visit) {
  int perRow = 0, rowCount = 0;
  if (!buffer || !r.getRegionRows(x, y, w, h, perRow, rowCount) || perRow <= 0 || perRow > static_cast<int>(STRIP_BYTES))
    return false;
  const int stripRows = static_cast<int>(STRIP_BYTES) / perRow;
  for (int first = 0; first < rowCount; first += stripRows) {
    const int rows = std::min(stripRows, rowCount - first);
    if (!visit(first, rows, static_cast<size_t>(rows) * perRow)) return false;
  }
  return true;
}

// sink(const uint8_t* bytes, size_t count) for the region's bytes in order (a hash, a file write).
template <class Renderer, class Sink>
bool each(const Renderer& r, const int x, const int y, const int w, const int h, const Buffer& buffer, Sink&& sink) {
  return strips(r, x, y, w, h, buffer, [&](const int first, const int rows, const size_t bytes) {
    return r.copyRegionRowsToBuffer(x, y, w, h, first, rows, buffer.data, STRIP_BYTES) && sink(buffer.data, bytes);
  });
}

// source(uint8_t* into, size_t count) fills each strip in order (a file read); the strip then goes to the screen buffer.
template <class Renderer, class Source>
bool fill(const Renderer& r, const int x, const int y, const int w, const int h, const Buffer& buffer, Source&& source) {
  return strips(r, x, y, w, h, buffer, [&](const int first, const int rows, const size_t bytes) {
    return source(buffer.data, bytes) && r.copyBufferToRegionRows(x, y, w, h, first, rows, buffer.data, STRIP_BYTES);
  });
}

}  // namespace framestrip
