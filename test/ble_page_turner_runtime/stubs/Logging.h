#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace ble_runtime_test {
inline std::vector<std::string> logs;

inline void clearLogs() { logs.clear(); }

inline void log(const char* origin, const char* format, ...) {
  char message[512] = {};
  va_list args;
  va_start(args, format);
  std::vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  logs.emplace_back(std::string(origin) + ":" + message);
}
}  // namespace ble_runtime_test

#define LOG_ERR(origin, format, ...) ::ble_runtime_test::log(origin, format, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) ::ble_runtime_test::log(origin, format, ##__VA_ARGS__)
#define LOG_DBG(origin, format, ...) ::ble_runtime_test::log(origin, format, ##__VA_ARGS__)
