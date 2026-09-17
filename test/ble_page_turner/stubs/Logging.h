#pragma once

// Host stand-in for lib/Logging/Logging.h. The real header pulls in
// Arduino/HardwareSerial/Print and redefines Serial; this suite asserts how many
// page turns a BLE key stream produces, not log text, so every level is a no-op.

template <typename... Args>
inline void blePageTurnerTestLog(const Args&...) {}

#define LOG_ERR(...) blePageTurnerTestLog(__VA_ARGS__)
#define LOG_INF(...) blePageTurnerTestLog(__VA_ARGS__)
#define LOG_DBG(...) blePageTurnerTestLog(__VA_ARGS__)
#define LOG_WARN(...) blePageTurnerTestLog(__VA_ARGS__)
