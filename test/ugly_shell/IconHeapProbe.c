#include <stdbool.h>
#include <stdlib.h>

static bool watching;
static unsigned allocations;

void uglyWatchHeap(bool enabled) {
  if (enabled) allocations = 0;
  watching = enabled;
}

unsigned uglyHeapCount(void) { return allocations; }

static void* probeMalloc(size_t size) {
  if (watching) ++allocations;
  return malloc(size);
}

static void* probeCalloc(size_t count, size_t size) {
  if (watching) ++allocations;
  return calloc(count, size);
}

static void* probeRealloc(void* memory, size_t size) {
  if (watching) ++allocations;
  return realloc(memory, size);
}

static void* probeAlignedAlloc(size_t alignment, size_t size) {
  if (watching) ++allocations;
  return aligned_alloc(alignment, size);
}

static int probePosixMemalign(void** memory, size_t alignment, size_t size) {
  if (watching) ++allocations;
  return posix_memalign(memory, alignment, size);
}

__attribute__((used, section("__DATA,__interpose")))
static const struct {
  const void* replacement;
  const void* original;
} heapProbes[] = {
  {(const void*)probeMalloc, (const void*)malloc},
  {(const void*)probeCalloc, (const void*)calloc},
  {(const void*)probeRealloc, (const void*)realloc},
  {(const void*)probeAlignedAlloc, (const void*)aligned_alloc},
  {(const void*)probePosixMemalign, (const void*)posix_memalign}
};
