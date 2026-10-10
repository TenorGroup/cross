#pragma once

// heap57 step 0: every allocation of 4 KB or more while the radio runs, or that leaves the heap
// under the floor (HeapFloor.h), with its size, capabilities, task and the heap after it; and the
// bytes the radio's own tasks asked for since boot. Records are
// taken inside the allocator wrappers (-Wl,--wrap) and printed from the loop. Measurement builds only.
#include <cstddef>
#include <cstdint>

#include <HeapFloor.h>

namespace heapbig {
constexpr size_t kBigBytes = 4096;
// The one rule for what is recorded: every allocation from 4 KB up (the peaks of each path); and,
// while the reader holds the grayscale strip, another task's allocation from 1 KB up or any that failed.
constexpr size_t kStripWindowBytes = 1024;
constexpr bool record(const size_t bytes, const bool otherTaskInStripWindow = false, const bool ok = true) {
  return bytes >= kBigBytes || (otherTaskInStripWindow && (bytes >= kStripWindowBytes || !ok));
}
// Tasks the radio owns: ble-start, ble-conn, the NimBLE host and the controller.
constexpr bool radioTaskName(const char* n) {
  return n != nullptr && ((n[0] == 'b' && n[1] == 'l' && n[2] == 'e') || (n[0] == 'n' && n[1] == 'i' && n[2] == 'm') ||
                          ((n[0] == 'b' || n[0] == 'B') && (n[1] == 't' || n[1] == 'T')));
}
}  // namespace heapbig

#if defined(TENOR_HEAP_BIG) && !defined(SIMULATOR)
void heapBigProbeTick(bool radioRunning);
// The reader took (true) or gave back (false) the grayscale strip on this task.
void heapBigStripWindow(bool held);
#else
inline void heapBigProbeTick(bool) {}
inline void heapBigStripWindow(bool) {}
#endif
