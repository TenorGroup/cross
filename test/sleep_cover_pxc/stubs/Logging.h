#pragma once
inline void downscaleTestLog(const char*, const char*, ...) {}
#define LOG_ERR(...) downscaleTestLog(__VA_ARGS__)
#define LOG_DBG(...) downscaleTestLog(__VA_ARGS__)
#define LOG_INF(...) downscaleTestLog(__VA_ARGS__)
