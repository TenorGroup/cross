#pragma once
#include "Memory.h"

// The door for allocations from 4 KB up: the host heap has no floor, so it only forwards.
namespace heapfloor {
template <typename T>
std::unique_ptr<T> array(const size_t count, const char*) {
  return makeUniqueNoThrow<T>(count);
}
}  // namespace heapfloor
