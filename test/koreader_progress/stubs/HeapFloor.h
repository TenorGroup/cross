#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace heapfloor {

constexpr size_t kBigBytes = 4096;
inline bool allow = true;
inline size_t calls = 0;
inline size_t lastBytes = 0;

template <typename T>
std::unique_ptr<T> array(const size_t count, const char*) {
  static_assert(std::is_array<T>::value && std::extent<T>::value == 0, "array<T[]>");
  using Elem = std::remove_extent_t<T>;
  const size_t bytes = count * sizeof(Elem);
  ++calls;
  lastBytes = bytes;
  if (!allow) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) Elem[count]());
}

}  // namespace heapfloor
