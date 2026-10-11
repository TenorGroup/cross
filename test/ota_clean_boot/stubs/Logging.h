#pragma once
#include <cstdint>
#include <cstring>
inline uint32_t loggedProgressFrames = 0;
template <class... Args>
inline void probeLog(const char*, const char*, Args&&...) {}
inline void probeLog(const char*, const char* format, uint32_t count) {
  if (std::strcmp(format, "Progress frames=%u") == 0) loggedProgressFrames = count;
}
// Arguments are evaluated, as on the device: a value kept only for a log line stays used.
template <class... Args>
inline void logSink(const char*, const char*, Args&&...) {}
#define LOG_DBG(...) logSink(__VA_ARGS__)
#define LOG_INF(...) logSink(__VA_ARGS__)
#define LOG_ERR(...) logSink(__VA_ARGS__)
#define LOG_PROBE(...) probeLog(__VA_ARGS__)
