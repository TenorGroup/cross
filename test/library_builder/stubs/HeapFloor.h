#pragma once

// The heap floor door on the host: the allocation goes through this test's makeUniqueNoThrow, so its
// counted failures land where they did before the door.
#include "Memory.h"

namespace heapfloor {
template <typename T>
std::unique_ptr<T> array(const size_t count, const char*) {
  return makeUniqueNoThrow<T>(count);
}
}  // namespace heapfloor
