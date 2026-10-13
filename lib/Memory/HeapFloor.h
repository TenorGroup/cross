#pragma once

// The heap floor (founder 09/10/2026, release rule 20) and the one door for every allocation from
// 4 KB up.
//
// The floor: the general heap keeps at least kFreeFloor bytes free AND a largest block that serves
// the reader's 7,920 B grayscale strip, a block of 8,192 B. It is measured before the strip is asked
// for (the floor keeps that block's room) and after every other allocation; not while the strip is
// held. The numbers live here once; lowering them needs a written founder decision.
//
// The door: array(), make() and take() allocate, then ask judge() whether the heap left is still on
// the floor. The grayscale strip and planes decide before they allocate, with hasGrayscaleHeapFloor()
// (TextGrayPolicy.h), which asks crosses() here. When it is not, the block is given back at once and the caller gets nullptr, which every
// caller already handles (no smoothing, no snapshot, a read error, a page drawn without its image).
// Nothing aborts. The check runs after the allocation, so two tasks that allocate at once can only
// see a lower heap than either alone: the door may refuse more, never less.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#if defined(__has_include)
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif
#endif

namespace heapfloor {

constexpr size_t kFreeFloor = 10240;
constexpr size_t kLargestFloor = 8192;  // the TLSF class of block the 7,920 B strip takes
// The heap API reports the largest request that fits: the block minus the light-poisoning overhead
// (multi_heap_poisoning.c). A block of exactly 8,192 B reports 8,180. Reported numbers compare here.
#if defined(CONFIG_HEAP_POISONING_DISABLED) && CONFIG_HEAP_POISONING_DISABLED
constexpr size_t kPoisonBytes = 0;
#else
constexpr size_t kPoisonBytes = 12;
#endif
constexpr size_t kLargestFloorReported = kLargestFloor - kPoisonBytes;
// Allocations below this size do not go through the door.
constexpr size_t kBigBytes = 4096;
// The grayscale strip the floor keeps room for, Home and reader alike: 80 rows of the widest panel
// without PSRAM (X3, 792 px = 99 B a row) are 7,920 B, inside the 8,192 B block above.
constexpr int kGrayStripRows = 80;
constexpr size_t kWidestPanelRowBytes = 99;
static_assert(kWidestPanelRowBytes * kGrayStripRows <= kLargestFloorReported,
              "the grayscale strip must fit the block the floor keeps");

// True when this heap state (as the heap API reports it) is under the floor.
constexpr bool crosses(const size_t freeBytes, const size_t largestReported) {
  return freeBytes < kFreeFloor || largestReported < kLargestFloorReported;
}

// Before work that will take `need` bytes in all, `biggest` in one block: true when the heap now
// (reported numbers) leaves the floor standing after them. A forecast that spares a step its
// earlier, costly part; each block still goes through the door below.
constexpr bool leaves(const size_t need, const size_t biggest, const size_t freeNow, const size_t largestNow) {
  return freeNow >= need && largestNow >= biggest && !crosses(freeNow - need, largestNow - biggest);
}

// THE decision for an allocation of `bytes`, given the heap left after it (reported numbers).
constexpr bool stands(const size_t bytes, const size_t freeAfter, const size_t largestAfterReported) {
  return bytes < kBigBytes || !crosses(freeAfter, largestAfterReported);
}

// Blocks the door has refused since boot. A step that sees the count move while it ran knows its
// failure was the heap's, and comes back later instead of counting a failure.
inline std::atomic<uint32_t> refusals{0};

#if defined(ESP_PLATFORM) && !defined(SIMULATOR)
// malloc(bytes) through the door; release with free().
void* take(size_t bytes, const char* who);
// Reads the heap left after `block` (just allocated) and answers stands(); a refusal is logged with
// `who`. A block outside internal RAM (PSRAM) always stands.
bool judge(size_t bytes, const void* block, const char* who);
#else
// Host tests and the simulator have no heap to keep.
inline bool judge(size_t, const void*, const char*) { return true; }
inline void* take(const size_t bytes, const char*) { return malloc(bytes); }
#endif

// makeUniqueNoThrow<T[]>(count) through the door (value-initialized, released with delete[]).
template <typename T>
std::unique_ptr<T> array(const size_t count, const char* who) {
  static_assert(std::is_array<T>::value && std::extent<T>::value == 0, "array<T[]>");
  using Elem = std::remove_extent_t<T>;
  std::unique_ptr<T> p(new (std::nothrow) Elem[count]());
  if (p && !judge(count * sizeof(Elem), p.get(), who)) p.reset();
  return p;
}

// makeUniqueNoThrow<T>(args...) through the door.
template <typename T, typename... Args>
std::unique_ptr<T> make(const char* who, Args&&... args) {
  static_assert(!std::is_array<T>::value, "make<T>");
  std::unique_ptr<T> p(new (std::nothrow) T(std::forward<Args>(args)...));
  if (p && !judge(sizeof(T), p.get(), who)) p.reset();
  return p;
}

}  // namespace heapfloor
