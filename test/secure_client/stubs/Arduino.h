#pragma once
#include "../../secure_http/stubs/Arduino.h"
struct TestSerial {
  explicit operator bool() const { return false; }
  template <class... T>
  void printf(const char*, T...) {}
  void println(const char*) {}
};
inline TestSerial Serial;
struct TestEsp {
  unsigned getFreeHeap() const { return 100000; }
  unsigned getMaxAllocHeap() const { return 100000; }
};
inline TestEsp ESP;
