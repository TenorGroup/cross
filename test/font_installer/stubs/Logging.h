#pragma once

// Host stand-in for lib/Logging/Logging.h (needs Arduino/HardwareSerial).
// FontInstaller logs only on rejection or failure, and the message text is not
// part of the contract asserted here, so every level is a no-op.

template <typename... Args>
inline void fontInstallerTestLog(const Args&...) {}

#define LOG_ERR(...) fontInstallerTestLog(__VA_ARGS__)
#define LOG_INF(...) fontInstallerTestLog(__VA_ARGS__)
#define LOG_DBG(...) fontInstallerTestLog(__VA_ARGS__)
