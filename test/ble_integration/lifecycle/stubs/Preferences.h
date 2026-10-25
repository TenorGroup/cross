#pragma once
// SdkRadio.cpp reads the saved bond through Preferences; this harness has none saved.
#include <cstddef>
#include <cstdint>
class Preferences {
 public:
  bool begin(const char*, bool = false) { return false; }
  void end() {}
  uint8_t getUChar(const char*, uint8_t value = 0) { return value; }
  size_t getBytes(const char*, void*, size_t) { return 0; }
};
