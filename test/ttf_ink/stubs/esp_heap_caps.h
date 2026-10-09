#pragma once
#include <cstddef>
inline constexpr int MALLOC_CAP_SPIRAM = 1;
inline constexpr int MALLOC_CAP_INTERNAL = 2;
inline constexpr int MALLOC_CAP_8BIT = 4;
inline size_t heap_caps_get_largest_free_block(int) { return 16 * 1024 * 1024; }
inline size_t heap_caps_get_free_size(int) { return 16 * 1024 * 1024; }
