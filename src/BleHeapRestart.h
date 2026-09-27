#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// A heap in pieces keeps the page-turner radio off for good: the stack needs one 32 KiB block,
// and leaving the book does not give it back (X3, 27/09/2026: free=87308 largest=23540 in the
// book and on Home alike, 61428 after a restart). A restart into the same book is the cure. It
// is allowed once until the radio comes up again, so a heap already in pieces after the restart
// cannot restart the device in a loop. Kept apart so the host tests can run it across a pretend
// restart.
namespace bleheap {

// The radio start needs both (BlePageTurnerRuntime.cpp).
constexpr size_t kMinimumFreeBytes = 65536;
constexpr size_t kMinimumLargestBlockBytes = 32768;
constexpr uint8_t kRefusalsBeforeRestart = 3;
constexpr uint32_t kRestartSpentMagic = 0x42485231u;

// Enough bytes in total, but no block the stack can take. A heap short in total is not this: a
// restart cannot give back bytes the book really uses.
constexpr bool fragmented(const size_t freeBytes, const size_t largestBlockBytes) {
  return freeBytes >= kMinimumFreeBytes && largestBlockBytes < kMinimumLargestBlockBytes;
}

// The one decision: restart to hand the radio a whole heap.
constexpr bool shouldRestart(const size_t freeBytes, const size_t largestBlockBytes, const uint8_t fragmentedInRow,
                             const bool restartedSinceRadioUp) {
  return !restartedSinceRadioUp && fragmented(freeBytes, largestBlockBytes) &&
         fragmentedInRow >= kRefusalsBeforeRestart;
}

// Kept in RTC memory, so it outlives ESP.restart. Power-on garbage is anything but the magic and
// reads as "no restart spent".
struct Memo {
  uint32_t magic;
};

// One per boot; only `memo` outlives the restart.
struct Tracker {
  Memo& memo;
  uint8_t fragmentedInRow = 0;
  // Asked by a refusal, read by the main loop, which restarts once a page is shown. The radio
  // start runs on its own task.
  std::atomic<bool> wanted{false};

  // A start the heap check refused. True when this refusal asks for the restart.
  bool refused(const size_t freeBytes, const size_t largestBlockBytes) {
    if (!fragmented(freeBytes, largestBlockBytes)) {
      fragmentedInRow = 0;
      wanted.store(false, std::memory_order_release);
    } else if (fragmentedInRow < UINT8_MAX) {
      ++fragmentedInRow;
    }
    const bool restart =
        shouldRestart(freeBytes, largestBlockBytes, fragmentedInRow, memo.magic == kRestartSpentMagic);
    if (restart) wanted.store(true, std::memory_order_release);
    return restart;
  }
  // The heap check passed: a rollback or a stack failure after it is not fragmentation, and a
  // restart asked earlier (another book, before Home) is no longer needed.
  void passed() {
    fragmentedInRow = 0;
    wanted.store(false, std::memory_order_release);
  }
  void radioUp() {
    fragmentedInRow = 0;
    memo.magic = 0;
    wanted.store(false, std::memory_order_release);
  }
  void restarting() {
    memo.magic = kRestartSpentMagic;
    wanted.store(false, std::memory_order_release);
  }
};

}  // namespace bleheap
