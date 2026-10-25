// RadioPort over the SDK's BleKeyboardHost, with the radio start on a FreeRTOS task. The only
// file of the module that includes the SDK or the chip's headers.
#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST

#include <BleKeyboardHost.h>
#include <esp_attr.h>
#include <Preferences.h>
#include <esp_timer.h>

#include <atomic>
#include <cstring>

#include "RadioPort.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace bleturner::port {
namespace {

// Looked up once: every call below is then a plain member call.
freeink::BleKeyboardHost& radioHost = freeink::BleKeyboardHost::getInstance();
freeink::BleKeyboardHost& hid() { return radioHost; }

// Outlives ESP.restart: the one heap restart already spent since the radio last came up.
RTC_NOINIT_ATTR Memo memo;

#ifdef BLE_PAGE_TURNER_STACK_PROBE
std::atomic<uint32_t> startStackMinimum{0};
#endif

void startTaskEntry(void*) {
  detail::startTask();
#ifdef BLE_PAGE_TURNER_STACK_PROBE
  startStackMinimum.store(uxTaskGetStackHighWaterMark(nullptr), std::memory_order_release);
#endif
  vTaskDelete(nullptr);
}

Peer peerOf(const char* addr, const char* name) { return Peer{addr, name}; }

}  // namespace

bool compiledIn() { return true; }
bool begin() { return hid().begin("FreeInk"); }
bool end(const uint32_t timeoutMs) { return hid().end(timeoutMs); }
bool running() { return hid().isRunning(); }
bool stopping() { return hid().isStopping(); }
bool connected() { return hid().isConnected(); }
bool connecting() { return hid().isConnecting(); }
ConnectionActivity connectionActivity() {
  const auto activity = hid().reconnectActivity();
  return {activity.busy, activity.advertisements};
}
bool scanning() { return hid().isScanning(); }
void poll() { hid().poll(); }

bool popRaw(RawEdge& out) {
  freeink::RawButtonEvent ev;
  if (!hid().popRawButton(ev)) return false;
  out.reportId = ev.reportId;
  out.byteIndex = ev.byteIndex;
  out.value = ev.value;
  out.pressed = ev.pressed;
  out.keycode = ev.keycode;
  out.mods = ev.mods;
  out.wasRest = ev.wasRest;
  out.atMs = ev.atMs;
  return true;
}

bool popKey(KeyPress& out) {
  freeink::KeyEvent ev;
  if (!hid().popKey(ev)) return false;
  out.keycode = ev.keycode;
  out.mods = ev.mods;
  out.pressed = ev.pressed;
  return true;
}

bool armBondedReconnect(uint8_t policy, const char* priorityAddr) {
  return hid().armBondedReconnect(policy == 1 ? freeink::PickPolicy::First : freeink::PickPolicy::Priority, priorityAddr);
}
Peer linked() { return peerOf(hid().connectedAddr(), hid().connectedName()); }

void scan(const uint32_t durationMs) {
  if (durationMs == 0) {
    hid().stopScan();
  } else {
    hid().startScan(durationMs);
  }
}

bool connect(const char* addr) { return hid().connect(addr); }
void disconnect() { hid().disconnect(); }
void forget(const char* addr) { hid().forget(addr); }
bool takeConnectFailure(char* out, const size_t outLen) { return hid().takeConnectFailure(out, outLen); }
uint8_t bondCount() { return hid().pairedCount(); }
Peer bond(const uint8_t index) { return peerOf(hid().paired(index).addr, hid().paired(index).name); }
bool firstSavedBond(char* addr, const size_t addrLen, char* name, const size_t nameLen) {
  // Namespace "freeink-hid": "n" the count, "b" the list of PairedHidDevice, at most kMaxBonds (the SDK's own layout).
  Preferences saved;
  if (!saved.begin("freeink-hid", true)) return false;
  freeink::PairedHidDevice bonds[freeink::BleKeyboardHost::kMaxBonds] = {};
  const uint8_t count = saved.getUChar("n", 0);
  const bool ok = count > 0 && count <= freeink::BleKeyboardHost::kMaxBonds &&
                  saved.getBytes("b", bonds, count * sizeof(bonds[0])) == count * sizeof(bonds[0]);
  saved.end();
  if (!ok) return false;
  strncpy(addr, bonds[0].addr, addrLen - 1);
  addr[addrLen - 1] = '\0';
  strncpy(name, bonds[0].name, nameLen - 1);
  name[nameLen - 1] = '\0';
  return true;
}
uint8_t foundCount() { return hid().deviceCount(); }
Peer found(const uint8_t index) { return peerOf(hid().device(index).addr, hid().device(index).name); }

bool spawnStart() { return xTaskCreate(startTaskEntry, "ble-start", 3072, nullptr, 2, nullptr) == pdPASS; }
uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }
void sleepMs(const uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
Memo& restartMemo() { return memo; }

#ifdef BLE_PAGE_TURNER_PROBE
void inject(const uint8_t* frame, const size_t len) { hid().onReportIngest(frame, len); }
unsigned rawOverflows() { return hid().rawOverflows(); }
#endif
#ifdef BLE_PAGE_TURNER_STACK_PROBE
uint32_t startStackLeft() { return startStackMinimum.load(std::memory_order_acquire); }
#endif

}  // namespace bleturner::port

#endif
