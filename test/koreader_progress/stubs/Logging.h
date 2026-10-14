#pragma once
inline void logDiscard(...) {}
#define LOG_DBG(...) logDiscard(__VA_ARGS__)
#define LOG_INF(...) logDiscard(__VA_ARGS__)
#define LOG_ERR(...) logDiscard(__VA_ARGS__)
