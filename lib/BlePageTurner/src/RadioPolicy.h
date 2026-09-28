#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "BlePageTurner.h"

// The page turner's rules, pure and constexpr so the tests can run them with a pretend clock
// and a pretend heap.
namespace bleturner {

// The stack needs both to start; after it started the reader still needs one 32 KiB block
// (InflateReader::RING_BYTES and the streaming miniz window).
inline constexpr size_t kMinimumFreeBytes = 65536;
inline constexpr size_t kMinimumLargestBlockBytes = 32768;
// A radio nobody connected to for this long stops: left on, it keeps the CPU at full speed
// and a device lying still eats its battery.
inline constexpr uint32_t kIdleOffMs = 5u * 60u * 1000u;

// Where the question is asked.
enum class Phase : uint8_t {
  Idle,         // may this book visit ask for the radio (asked once per visit, running or not)
  BeforeStart,  // on the start task, caches released, right before the stack comes up
  JustStarted,  // the stack came up: keep it, or roll it back
  Running,      // the radio is up: keep it?
};

struct RadioInputs {
  Phase phase;
  bool enabled;
  Where where;
  bool pageShown;
  bool bookIndexing;
  bool storageBusy;
  bool heldForBuild;  // beforeChapterBuild() stopped it and afterPaint() has not come yet
  bool idleStopped;   // stopped for idleness and not asked again since
  bool linked;        // a remote is connected
  uint32_t idleMs;    // since a remote was last connected (or the radio came up)
  Heap heap;
};

// The ONE answer to "may the radio be on now". Everything that starts, keeps or stops the
// radio asks here.
constexpr Why radioVerdict(const RadioInputs& in) {
  const bool stackFits = in.heap.freeBytes >= kMinimumFreeBytes && in.heap.largestBlock >= kMinimumLargestBlockBytes;
  switch (in.phase) {
    case Phase::Idle:
      if (!in.enabled) return Why::Off;
      if (in.storageBusy) return Why::StorageBusy;
      // Pairing is the user asking for the radio now: the book's conditions do not apply.
      if (in.where == Where::PairingScreen) return Why::Ok;
      if (in.where != Where::Reader) return Why::NotReading;
      if (!in.pageShown) return Why::PageNotShown;
      // A radio stopped for a starved build waits for the page, not for a key: restarting it
      // on a key release starved the build again and the start held the loop for 2.85 s (X3,
      // 23/09/2026).
      if (in.heldForBuild) return Why::BuildNeedsHeap;
      if (in.idleStopped) return Why::IdleNoLink;
      // A book still building its index needs the heap the radio would take; its keys work.
      if (in.bookIndexing) return Why::Indexing;
      return Why::Ok;
    case Phase::BeforeStart:
      if (in.storageBusy) return Why::StorageBusy;
      if (stackFits) return Why::Ok;
      // Enough bytes in total, but no block the stack can take. A heap short in total is not
      // this: a restart cannot give back bytes the book really uses.
      return in.heap.freeBytes >= kMinimumFreeBytes ? Why::HeapInPieces : Why::HeapLow;
    case Phase::JustStarted:
      if (in.storageBusy) return Why::StorageBusy;
      // The stack may come up and take the reader's last large block.
      return in.heap.largestBlock >= kMinimumLargestBlockBytes ? Why::Ok : Why::HeapLow;
    case Phase::Running:
      if (!in.enabled) return Why::Off;
      if (in.storageBusy) return Why::StorageBusy;
      // A linked remote is never idle: it sits quiet between two presses for a long time.
      if (!in.linked && in.idleMs >= kIdleOffMs) return Why::IdleNoLink;
      return Why::Ok;
  }
  return Why::Off;
}

// --- The heap restart ------------------------------------------------------------
// A heap in pieces keeps the radio off for good: leaving the book does not give the block
// back (X3, 27/09/2026: free=87308 largest=23540 in the book and on Home alike, 61428 after
// a restart). A restart into the same book is the cure. It is allowed once until the radio
// comes up again, so a heap still in pieces after the restart cannot restart the device in a
// loop.
inline constexpr uint8_t kRefusalsBeforeRestart = 3;
inline constexpr uint32_t kRestartSpentMagic = 0x42485231u;

constexpr bool fragmented(const size_t freeBytes, const size_t largestBlock) {
  RadioInputs in{};
  in.phase = Phase::BeforeStart;
  in.heap = {freeBytes, largestBlock};
  return radioVerdict(in) == Why::HeapInPieces;
}

// The second decision, kept apart: restart to hand the radio a whole heap.
constexpr bool shouldRestart(const size_t freeBytes, const size_t largestBlock, const uint8_t fragmentedInRow,
                             const bool restartedSinceRadioUp) {
  return !restartedSinceRadioUp && fragmented(freeBytes, largestBlock) && fragmentedInRow >= kRefusalsBeforeRestart;
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
  // Asked by a refusal on the start task, read by the main loop, which restarts once a page is
  // shown.
  std::atomic<bool> wanted{false};

  // A start the heap check refused. True when this refusal asks for the restart.
  bool refused(const size_t freeBytes, const size_t largestBlock) {
    if (!fragmented(freeBytes, largestBlock)) {
      fragmentedInRow = 0;
      wanted.store(false, std::memory_order_release);
    } else if (fragmentedInRow < UINT8_MAX) {
      ++fragmentedInRow;
    }
    const bool restart = shouldRestart(freeBytes, largestBlock, fragmentedInRow, memo.magic == kRestartSpentMagic);
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

}  // namespace bleturner
