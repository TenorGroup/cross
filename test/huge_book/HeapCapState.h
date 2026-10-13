#pragma once
// Heap model state for the huge-book tests (see HeapCap.h). Stubs read the
// remaining budget from here so ESP.getFreeHeap() matches the cap.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace heapcap {
inline bool counting = false;   // count allocations made while true
inline size_t cap = SIZE_MAX;   // bytes available to counted allocations
inline size_t live = 0;         // counted bytes currently allocated
inline size_t peak = 0;         // high-water mark of live since reset()
inline size_t biggest = 0;      // largest single counted allocation since reset()
inline unsigned aborts = 0;     // throwing allocations past the cap (device abort)
inline size_t firstAbortSize = 0, firstAbortLive = 0;
inline unsigned refusals = 0;   // nothrow allocations refused by the cap
inline int untracked = 0;       // > 0 while test storage allocates (not device heap)

// What the S3 would keep in internal RAM. There malloc and new above kS3InternalMax go to PSRAM
// (sdkconfig.h: CONFIG_SPIRAM_USE_MALLOC=1, CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096), so only the
// requests at or below it stay in the 320 KB that the radio shares with the reader.
constexpr size_t kS3InternalMax = 4096;
inline size_t internalLive = 0;     // counted bytes of requests <= kS3InternalMax now allocated
inline size_t internalPeak = 0;     // high-water mark of internalLive since reset()
inline size_t internalMax = 0;      // largest single request that would stay internal
inline unsigned psramCount = 0;     // requests above kS3InternalMax (PSRAM on S3)
inline size_t psramMax = 0;         // largest of them

inline void reset(size_t newCap) {
  cap = newCap;
  live = peak = biggest = 0;
  internalLive = internalPeak = internalMax = psramMax = 0;
  psramCount = 0;
  aborts = refusals = 0;
  firstAbortSize = firstAbortLive = 0;
  counting = true;
}
inline void stop() { counting = false; }
inline size_t available() { return live >= cap ? 0 : cap - live; }

// Test-side storage (the fake SD card) is not device heap.
struct Untracked {
  Untracked() { ++untracked; }
  ~Untracked() { --untracked; }
};

struct Header {
  size_t size;
  size_t counted;
  size_t pad[2];
};
static_assert(sizeof(Header) % alignof(std::max_align_t) == 0, "header keeps payload aligned");

inline void* allocate(size_t size, bool nothrow) {
  const bool count = counting && untracked == 0;
  if (count && size > available()) {
    if (nothrow) {
      ++refusals;
      return nullptr;
    }
    // The device aborts here. Keep running so the test can report where and
    // how far over the cap the request went instead of unwinding through C.
    if (aborts++ == 0) firstAbortSize = size, firstAbortLive = live;
  }
  auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + (size ? size : 1)));
  if (!header) {
    if (nothrow) return nullptr;
    throw std::bad_alloc();
  }
  header->size = size;
  header->counted = count ? 1 : 0;
  if (count) {
    live += size;
    if (live > peak) peak = live;
    if (size > biggest) biggest = size;
    if (size <= kS3InternalMax) {
      internalLive += size;
      if (internalLive > internalPeak) internalPeak = internalLive;
      if (size > internalMax) internalMax = size;
    } else {
      ++psramCount;
      if (size > psramMax) psramMax = size;
    }
  }
  return header + 1;
}

inline void release(void* p) {
  if (!p) return;
  auto* header = static_cast<Header*>(p) - 1;
  if (header->counted) {
    live -= header->size;
    if (header->size <= kS3InternalMax) internalLive -= header->size;
  }
  std::free(header);
}
}  // namespace heapcap
