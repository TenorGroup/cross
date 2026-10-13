#pragma once

#include <cstdint>

// Host-test stand-in for the vector font: remembers the outline strength FontCacheManager hands it.
#define CROSSPOINT_VECTOR_FONTS 1

class TtfEpdFont {
 public:
  void clearCache() {}
  void releaseResidentCaches() {}
  bool build(const char*) { return true; }
  bool addCoverage(const char*) { return true; }
  void setInkStrength(int32_t value, bool aa) {
    strength = value;
    antiAliased = aa;
  }

  int32_t strength = -1;
  bool antiAliased = false;
};
