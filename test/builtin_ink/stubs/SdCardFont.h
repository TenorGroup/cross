#pragma once
#include <cstdint>
class SdCardFont {
 public:
  void clearCache() {}
  void setReaderInk(uint8_t, bool) {}
  void releaseResidentCaches() {}
  int prewarm(const char*, uint8_t, bool, bool, bool) { return 0; }
  uint8_t resolveStyle(uint8_t style) const { return style & 3; }
  void logStats(const char*) {}
  void resetStats() {}
};
