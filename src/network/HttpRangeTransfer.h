#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace http_range {
inline constexpr size_t PART_BYTES = 192 * 1024;
inline constexpr uint32_t MAX_PART_RETRIES = 5;

template <typename Fetch>
bool transfer(size_t total, size_t& received, bool& cancelled, Fetch fetch, uint32_t& parts, uint32_t& retries) {
  parts = retries = 0;
  while (received < total && !cancelled) {
    const size_t first = received;
    const size_t last = first + std::min(total - first, PART_BYTES) - 1;
    bool whole = false;
    bool stop = false;
    const bool ok = fetch(first, last, whole, stop);
    ++parts;
    if (ok && received > first) continue;
    if (ok && !whole) break;
    if (cancelled || stop || whole) return false;
    if (++retries > MAX_PART_RETRIES) return false;
  }
  return !cancelled;
}
}
