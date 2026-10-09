#pragma once
#include <cstddef>
namespace freeink {
class MemoryManager {
 public:
  static MemoryManager& instance() { static MemoryManager manager; return manager; }
  bool ensureFree(size_t) { return true; }
};
}
