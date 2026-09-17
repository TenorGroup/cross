#pragma once

#include <cstdint>

#include "activities/RenderLock.h"
#include "HalMemory.h"

namespace freeink {

class BleKeyboardHost {
 public:
  static BleKeyboardHost& getInstance() {
    static BleKeyboardHost host;
    return host;
  }

  bool isRunning() const { return running; }
  bool isStopping() const { return stopping; }
  bool begin(const char*) {
    ++beginCalls;
    beginHeldRenderLock = ble_runtime_test::renderLockHeld;
    if (changeHeapOnBegin) HalMemory::internalHeap = heapAfterBegin;
    if (beginResult) running = true;
    return beginResult;
  }
  bool end(uint32_t timeoutMs = 1000) {
    lastEndTimeoutMs = timeoutMs;
    ++endCalls;
    endHeldRenderLock = ble_runtime_test::renderLockHeld;
    running = false;
    if (!endResult || pendingCleanup) {
      stopping = true;
      return false;
    }
    running = false;
    pendingCleanup = false;
    stopping = false;
    return true;
  }

  void reset() {
    running = false;
    stopping = false;
    beginHeldRenderLock = false;
    endHeldRenderLock = false;
    changeHeapOnBegin = false;
    beginResult = true;
    endResult = true;
    pendingCleanup = false;
    beginCalls = 0;
    endCalls = 0;
    lastEndTimeoutMs = 0;
  }

  bool running = false;
  bool stopping = false;
  bool beginHeldRenderLock = false;
  bool endHeldRenderLock = false;
  bool changeHeapOnBegin = false;
  HalMemory::HeapStats heapAfterBegin{};
  bool beginResult = true;
  bool endResult = true;
  bool pendingCleanup = false;
  unsigned beginCalls = 0;
  unsigned endCalls = 0;
  uint32_t lastEndTimeoutMs = 0;
};

}  // namespace freeink
