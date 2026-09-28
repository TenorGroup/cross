#pragma once
// Arguments are evaluated, as on the device: a value kept only for a log line stays used.
template <class... Args>
inline void logSink(const char*, const char*, Args&&...) {}
#define LOG_DBG(...) logSink(__VA_ARGS__)
#define LOG_INF(...) logSink(__VA_ARGS__)
#define LOG_ERR(...) logSink(__VA_ARGS__)
#define LOG_PROBE(...) logSink(__VA_ARGS__)
