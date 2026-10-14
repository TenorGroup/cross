#pragma once
// The heap as the X3 has it after a Bluetooth session: free heap enough, but no block over `largest`.
// take() is the one door for 4 KB and up, as in lib/Memory/HeapFloor.h; every block is counted.
#include <cstddef>
#include <cstdlib>
#include <map>

namespace heapfloor {
inline size_t largest = 28660;  // the largest block the heap can give (the log of the X3: largest=28660)
inline size_t biggestRequest = 0;
inline size_t liveBytes = 0, peakBytes = 0;
inline int refused = 0;
inline int takesLeft = -1;  // -1: no limit; else this many blocks, then the door refuses (a heap that runs out midway)
inline std::map<void*, size_t> blocks;
inline void* take(const size_t bytes, const char*) {
  if (bytes > biggestRequest) biggestRequest = bytes;
  if (bytes > largest || takesLeft == 0) { ++refused; return nullptr; }
  if (takesLeft > 0) --takesLeft;
  void* block = std::malloc(bytes);
  if (block) { blocks[block] = bytes; liveBytes += bytes; if (liveBytes > peakBytes) peakBytes = liveBytes; }
  return block;
}
inline void release(void* block) {
  const auto it = blocks.find(block);
  if (it != blocks.end()) { liveBytes -= it->second; blocks.erase(it); }
  std::free(block);
}
}  // namespace heapfloor
