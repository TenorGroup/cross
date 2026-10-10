#pragma once

// The X3 heap floor (founder 09/10/2026; tenor-cross-hieu-nang rule 20): at every paint mark and
// between them, with the radio on, the general heap keeps at least kFreeFloor bytes free AND a
// largest block of at least kLargestFloor bytes (the 7,920 B grayscale strip of every page paint
// takes a block of exactly that class). The numbers live here once; lowering them needs a written
// founder decision.
#include <cstddef>
#if defined(__has_include)
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif
#endif

namespace heapfloor {
constexpr size_t kFreeFloor = 10240;
constexpr size_t kLargestFloor = 8192;  // a free block of this TLSF class (what the strip takes)

// ESP.getMaxAllocHeap() and heap_caps_get_largest_free_block() report the largest request that fits,
// i.e. the block minus the light-poisoning overhead (multi_heap_poisoning.c:402). A block of exactly
// 8,192 B, which serves the 7,920 B strip, reports 8,180. Compare reported numbers with this.
#if defined(CONFIG_HEAP_POISONING_DISABLED) && CONFIG_HEAP_POISONING_DISABLED
constexpr size_t kPoisonBytes = 0;
#else
constexpr size_t kPoisonBytes = 12;
#endif
constexpr size_t kLargestFloorReported = kLargestFloor - kPoisonBytes;

// True when this heap state (as the heap API reports it) is under the floor.
constexpr bool crosses(const size_t freeBytes, const size_t largestReported) {
  return freeBytes < kFreeFloor || largestReported < kLargestFloorReported;
}
}  // namespace heapfloor
