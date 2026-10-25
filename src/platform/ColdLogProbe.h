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
  PullNetwork = 17,
  PullHeapRestart = 18,
};

enum class TraceStage : uint8_t {
  Hash = 1,
  JoinStart = 2,
  WifiHint = 3,
  WifiScan = 4,
  WifiConnected = 5,
  HttpHeap = 6,
  HttpUrl = 7,
  HttpNtp = 8,
  HttpBegin = 9,
  HttpTransport = 10,
  HttpReply = 11,
  HttpBody = 12,
  HttpParse = 13,
  PullMap = 14,
  PullSave = 15,
  RadioBefore = 16,
  RadioAfter = 17,
};

#if defined(TENOR_PRESS_PROBE) && FREEINK_DEVICE_X3 && !defined(SIMULATOR)
void boot(bool silent, uint32_t target, uint32_t payload);
void mark(Event event, uint32_t detail = 0, uint32_t aux = 0);
void traceReset();
void trace(TraceStage stage, int32_t code, uint32_t freeBytes, uint32_t largestBlock);
void dump();
uint32_t bootNumber();
#else
inline void boot(bool, uint32_t, uint32_t) {}
inline void mark(Event, uint32_t = 0, uint32_t = 0) {}
inline void traceReset() {}
inline void trace(TraceStage, int32_t, uint32_t, uint32_t) {}
inline void dump() {}
inline uint32_t bootNumber() { return 0; }
#endif

}  // namespace restartlog
