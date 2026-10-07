#pragma once
#include "Arduino.h"
class HardwareSerial {
 public:
  bool connected = false;
  unsigned writes = 0;
  std::string bytes;
  operator bool() const { return connected; }
  size_t print(const char* text) {
    ++writes;
    bytes += text;
    return strlen(text);
  }
  size_t write(const uint8_t* data, size_t len) {
    ++writes;
    bytes.append(reinterpret_cast<const char*>(data), len);
    return len;
  }
  size_t printf(const char* format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (count < 0) return 0;
    return print(text);
  }
  void begin(unsigned long) {}
};
extern HardwareSerial Serial;
