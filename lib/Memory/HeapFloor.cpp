#include "HeapFloor.h"

#if defined(ESP_PLATFORM) && !defined(SIMULATOR)
#include <Logging.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include <cstdlib>

namespace heapfloor {

bool judge(const size_t bytes, const void* block, const char* who) {
  if (bytes < kBigBytes || !esp_ptr_internal(block)) return true;
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const size_t freeBytes = heap_caps_get_free_size(caps);
  const size_t largest = heap_caps_get_largest_free_block(caps);
  if (stands(bytes, freeBytes, largest)) return true;
  refusals.fetch_add(1, std::memory_order_relaxed);
  LOG_INF("HEAP_FLOOR", "refuse who=%s bytes=%u free=%u largest=%u", who, static_cast<unsigned>(bytes),
          static_cast<unsigned>(freeBytes), static_cast<unsigned>(largest));
  return false;
}

void* take(const size_t bytes, const char* who) {
  void* p = std::malloc(bytes);
  if (p && !judge(bytes, p, who)) {
    std::free(p);
    return nullptr;
  }
  return p;
}

}  // namespace heapfloor
#endif
