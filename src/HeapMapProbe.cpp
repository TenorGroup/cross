#if defined(TENOR_PRESS_PROBE) && !defined(SIMULATOR)
#include "HeapMapProbe.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstring>

namespace {

struct Block {
  uintptr_t addr;
  uint32_t size;  // top bit set when used
  uint8_t head[4];
};

constexpr size_t kMaxBlocks = 512;
Block* blocks = nullptr;
size_t blockCount = 0;
size_t dropped = 0;

bool collect(walker_heap_into_t, walker_block_info_t info, void*) {
  if (blockCount >= kMaxBlocks) {
    dropped++;
    return true;
  }
  Block& b = blocks[blockCount++];
  b.addr = reinterpret_cast<uintptr_t>(info.ptr);
  b.size = static_cast<uint32_t>(info.size) | (info.used ? 0x80000000u : 0u);
  // Copying inside the walk keeps the bytes consistent with the block list.
  if (info.used && info.size >= 4) memcpy(b.head, info.ptr, 4);
  else memset(b.head, 0, 4);
  return true;
}

}  // namespace

void heapMapDump(const char* tag) {
  // Borrowed only for the dump: a static array would shrink the heap this probe measures.
  blocks = static_cast<Block*>(heap_caps_malloc(sizeof(Block) * kMaxBlocks, MALLOC_CAP_INTERNAL));
  if (!blocks) {
    Serial.printf("[HMAP] %s no room for the block list\n", tag);
    return;
  }
  blockCount = 0;
  dropped = 0;
  // The walk holds the heap lock: collect first, print after (printing inside tripped the watchdog).
  heap_caps_walk(MALLOC_CAP_INTERNAL, collect, nullptr);
  Serial.printf("[HMAP] BEGIN %s blocks=%u dropped=%u largest=%u free=%u\n", tag, static_cast<unsigned>(blockCount),
                static_cast<unsigned>(dropped), static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  for (size_t i = 0; i < blockCount; i++) {
    const Block& b = blocks[i];
    const bool used = b.size & 0x80000000u;
    const uint32_t size = b.size & 0x7fffffffu;
    if (used) {
      Serial.printf("[HMAP] %08x %6u U %02x%02x%02x%02x\n", static_cast<unsigned>(b.addr),
                    static_cast<unsigned>(size), b.head[0], b.head[1], b.head[2], b.head[3]);
    } else {
      Serial.printf("[HMAP] %08x %6u F\n", static_cast<unsigned>(b.addr), static_cast<unsigned>(size));
    }
    if (i % 16 == 15) delay(2);
  }
  Serial.printf("[HMAP] END %s list=%08x\n", tag, static_cast<unsigned>(reinterpret_cast<uintptr_t>(blocks)));
  heap_caps_free(blocks);
  blocks = nullptr;
}
#endif
