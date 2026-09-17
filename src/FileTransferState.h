#pragma once

#include <stdint.h>

#include <BoardConfig.h>

#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
#include <BleKeyboardHost.h>
#endif

// Runtime owner count for activities that share the radio and storage handoff.
// A parent can retain an owner while a nested activity is active.
namespace filetransfer {

inline uint8_t& ownerCount() {
  static uint8_t count = 0;
  return count;
}

inline bool isActive() { return ownerCount() != 0; }

inline bool stopBleHostIfRunning() {
#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
  auto& host = freeink::BleKeyboardHost::getInstance();
  // end() must also drain a host whose public running flag already dropped.
  return host.end();
#else
  return true;
#endif
}

inline bool acquire() {
  auto& count = ownerCount();
  if (count != UINT8_MAX) ++count;
  // Stop on every acquire so a nested activity cannot reintroduce the radio.
  return stopBleHostIfRunning();
}

inline void release() {
  auto& count = ownerCount();
  if (count != 0) --count;
}

}  // namespace filetransfer
