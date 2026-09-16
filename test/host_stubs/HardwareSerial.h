#pragma once
#include <Print.h>
class HardwareSerial : public Print {
 public:
  void begin(unsigned long) {}
  operator bool() const { return true; }
  template <typename... A> void printf(A...) {}
};
extern HardwareSerial Serial;
