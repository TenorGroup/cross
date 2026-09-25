#pragma once
inline void coverTestLog(const char*, const char*, ...) {}
#define LOG_ERR(...) coverTestLog(__VA_ARGS__)
#define LOG_DBG(...) coverTestLog(__VA_ARGS__)
#define LOG_INF(...) coverTestLog(__VA_ARGS__)
