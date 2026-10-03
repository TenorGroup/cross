#pragma once

// Host-test stand-in for FreeInkFont's PSRAM-preferring helpers: plain heap, same contracts.
// A freed block is filled with 0xDD first, so a pointer kept across a cache release reads
// garbage even without AddressSanitizer.
#include <cstring>
#include <new>
#include <unordered_map>
#include <vector>

namespace freeink {
namespace font {

template <typename T>
using PsramVector = std::vector<T>;

inline std::unordered_map<const void*, std::size_t>& liveBlocks() {
  static std::unordered_map<const void*, std::size_t> blocks;
  return blocks;
}

template <typename T>
T* psramNewArray(std::size_t n) {
  T* p = new (std::nothrow) T[n ? n : 1];
  if (p) liveBlocks()[p] = (n ? n : 1) * sizeof(T);
  return p;
}

template <typename T>
void psramDeleteArray(T* p) {
  if (!p) return;
  const auto it = liveBlocks().find(p);
  if (it != liveBlocks().end()) {
    std::memset(static_cast<void*>(p), 0xDD, it->second);
    liveBlocks().erase(it);
  }
  delete[] p;
}

}  // namespace font
}  // namespace freeink
