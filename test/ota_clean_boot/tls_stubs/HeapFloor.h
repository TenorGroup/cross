#pragma once
#include <cstddef>
namespace heapfloor {
constexpr size_t kBigBytes = 4096;
void* take(size_t bytes, const char* who);
}
