#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

class HalMemory {
 public:
  inline static size_t calls = 0;
  inline static size_t live = 0;
  inline static size_t largest = 0;
  inline static size_t failAt = 0;
  struct PsramDeleter {
    void operator()(uint8_t* block) const {
      if (block) { --live; std::free(block); }
    }
  };
  using PsramBuffer = std::unique_ptr<uint8_t[], PsramDeleter>;
  static PsramBuffer allocatePsram(size_t bytes) {
    ++calls;
    if (bytes > largest) largest = bytes;
    if (calls == failAt) return {};
    auto* block = static_cast<uint8_t*>(std::malloc(bytes));
    if (block) ++live;
    return PsramBuffer(block);
  }
};
