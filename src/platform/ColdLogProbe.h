#pragma once

#include <cstdint>

namespace restartlog {

enum class Event : uint8_t {
  Boot = 1,
  SilentRestart = 2,
  StorageRestart = 3,
  DeepSleepBegin = 4,
  DeepSleepReady = 5,
  FirmwareRestart = 6,
  OtaRestart = 7,
  RecoveryRestart = 8,
  FramebufferRestart = 9,
  BleHeapRestart = 10,
  SleepEnter = 11,
  PayloadProbe = 12,
  RestartNow = 13,
  PullStep = 14,
  StateRead = 15,
  StateWrite = 16,
};

#if defined(TENOR_PRESS_PROBE) && FREEINK_DEVICE_X3 && !defined(SIMULATOR)
void boot(bool silent, uint32_t target, uint32_t payload);
void mark(Event event, uint32_t detail = 0, uint32_t aux = 0);
void dump();
#else
inline void boot(bool, uint32_t, uint32_t) {}
inline void mark(Event, uint32_t = 0, uint32_t = 0) {}
inline void dump() {}
#endif

}  // namespace restartlog
