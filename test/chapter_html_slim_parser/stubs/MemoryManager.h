#pragma once

// Host-test stub for the SDK MemoryManager. The parser/layout path calls
// ensureFree() to evict rebuildable caches before large allocations; on the
// host there are no caches and no pressure, so it is a no-op that reports
// success. The renderer registers its glyph caches as sinks at begin(); the
// host never evicts, so registration is accepted and dropped. Matches only the
// surface the layout and renderer code uses.

#include <cstddef>
#include <cstdint>
#include <functional>

namespace freeink {

enum class MemPool : unsigned char { Internal, Psram, Default };

struct CacheSink {
  const char* name = nullptr;
  uint8_t priority = 128;
  std::function<size_t(size_t bytesRequested)> evict;
};

class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager inst;
    return inst;
  }
  bool ensureFree(size_t, MemPool = MemPool::Default) { return true; }
  int registerSink(const CacheSink&) { return 0; }
  size_t freeBytes(MemPool = MemPool::Default) const { return 0; }

 private:
  MemoryManager() = default;
};

}  // namespace freeink
