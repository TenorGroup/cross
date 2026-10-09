#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include "activities/RenderLock.h"
#include "HalMemory.h"
#include "HalPowerManager.h"
namespace freeink {
enum class PickPolicy : uint8_t { Priority = 0, First = 1 };
struct KeyEvent {
  uint8_t keycode = 0;
  uint8_t mods = 0;
  bool pressed = true;
};
struct RawButtonEvent {
  uint8_t reportId = 0;
  uint8_t byteIndex = 0;
  uint8_t value = 0;
  bool pressed = false;
  uint8_t keycode = 0;
  uint8_t mods = 0;
  bool wasRest = false;
  uint32_t atMs = 0;
};
struct PairedHidDevice {
  char addr[18] = {0};
  char name[32] = {0};
};
struct DiscoveredDevice {
  char addr[18] = {0};
  char name[32] = {0};
};
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
  // The rest of the surface the page turner's SDK wrapper uses; this harness does not drive it.
  bool isConnected() const { return false; }
  bool isConnecting() const { return false; }
  bool isScanning() const { return false; }
  void poll() {}
  bool popRawButton(RawButtonEvent&) { return false; }
  bool popKey(KeyEvent&) { return false; }
  bool armBondedReconnect(PickPolicy, const char*) { return false; }
  const char* connectedAddr() const { return ""; }
  const char* connectedName() const { return ""; }
  void startScan(uint32_t) {}
  void stopScan() {}
  bool connect(const char*) { return false; }
  void disconnect() {}
  void forget(const char*) {}
  bool takeConnectFailure(char*, size_t) { return false; }
  uint8_t pairedCount() const { return 0; }
  const PairedHidDevice& paired(uint8_t) const { return pairedNone; }
  uint8_t deviceCount() const { return 0; }
  const DiscoveredDevice& device(uint8_t) const { return deviceNone; }
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
  PairedHidDevice pairedNone;
  DiscoveredDevice deviceNone;
};
}
