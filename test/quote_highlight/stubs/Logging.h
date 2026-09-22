#pragma once
// The storage stub already defines LOG_ERR; the store also logs at info level. The
// arguments are still evaluated, as with serial logging on, so counters kept only for a
// log line do not read as unused.
inline void logDiscard(...) {}
#ifndef LOG_ERR
#define LOG_ERR(...) logDiscard(__VA_ARGS__)
#endif
#ifndef LOG_INF
#define LOG_INF(...) logDiscard(__VA_ARGS__)
#endif
