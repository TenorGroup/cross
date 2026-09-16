#pragma once
#include <cstdint>
#include <cstddef>
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) { return 0; }
  virtual size_t write(const uint8_t*, size_t) { return 0; }
  virtual void flush() {}
  size_t print(const char*) { return 0; }
  size_t println(const char*) { return 0; }
  size_t println() { return 0; }
};
