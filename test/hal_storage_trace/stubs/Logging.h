#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

inline std::vector<std::string> traceLines;
inline void logPrintf(const char*, const char* origin, const char* format, ...) {
  char buffer[256];
  const int prefix = std::snprintf(buffer, sizeof(buffer), "[%s] ", origin);
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer + prefix, sizeof(buffer) - prefix, format, args);
  va_end(args);
  traceLines.emplace_back(buffer);
}
#define LOG_INF(origin, format, ...) logPrintf("INF", origin, format "\n", ##__VA_ARGS__)
#define LOG_ERR(origin, format, ...) logPrintf("ERR", origin, format "\n", ##__VA_ARGS__)
