#pragma once
inline void pngTestLog(const char*, const char*, ...) {}
#define LOG_ERR(...) pngTestLog(__VA_ARGS__)
#define LOG_DBG(...) pngTestLog(__VA_ARGS__)
