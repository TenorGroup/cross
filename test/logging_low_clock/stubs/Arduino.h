#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#define RTC_NOINIT_ATTR
inline unsigned long millis() { return 85; }
using String = std::string;
class Print {
 public:
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual void flush() = 0;
};
