#pragma once

// The heap floor door on the host: nothing to keep, the allocation is the test's own makeUniqueNoThrow.
#include <cstddef>

#include "Memory.h"

namespace heapfloor {
constexpr size_t kBigBytes = 4096;
inline bool judge(size_t, const void*, const char*) { return true; }
template <typename T>
std::unique_ptr<T> array(const size_t count, const char*) {
  return makeUniqueNoThrow<T>(count);
}
}  // namespace heapfloor
