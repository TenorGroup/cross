#pragma once
#include <cstdint>
#include <functional>
#include "activities/RenderLock.h"
#include "HalMemory.h"
#include "HalPowerManager.h"
namespace freeink {
class BleKeyboardHost {
 public:
  static BleKeyboardHost& getInstance() { static BleKeyboardHost host; return host; }
  bool isRunning() const { return running; }
  bool isStopping() const { return stopping; }
  bool begin(const char*) {
    ++beginCalls;
    insideBegin = true;
    beginHadPower = power_test::locks > 0;
    if (beginHook) beginHook();
    if (changeHeapOnBegin) HalMemory::internalHeap = heapAfterBegin;
    if (beginResult) running = true;
    insideBegin = false;
    return beginResult;
  }
  bool end(uint32_t timeoutMs = 1000) {
    lastEndTimeoutMs = timeoutMs;
    ++endCalls;
    endDuringBegin = endDuringBegin || insideBegin;
    endHadPower = power_test::locks > 0;
    if (endHook) endHook();
    running = false;
    stopping = !endResult;
    return endResult;
  }
  bool running = false;
  bool stopping = false;
  bool insideBegin = false;
  bool endDuringBegin = false;
  bool beginHadPower = false;
  bool endHadPower = false;
  bool changeHeapOnBegin = false;
  HalMemory::HeapStats heapAfterBegin{};
  bool beginResult = true;
  bool endResult = true;
  unsigned beginCalls = 0;
  unsigned endCalls = 0;
  uint32_t lastEndTimeoutMs = 0;
  std::function<void()> beginHook;
  std::function<void()> endHook;
};
}
