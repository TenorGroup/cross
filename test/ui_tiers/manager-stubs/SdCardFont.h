#pragma once
#include <cstdint>
#include <cstdlib>
class SdCardFont {
  uint32_t hash_ = 0;
 public:
  inline static int alive = 0;
  SdCardFont() { ++alive; }
  ~SdCardFont() { --alive; }
  bool load(const char* path) { hash_ = std::strtoul(path, nullptr, 10); return hash_ != 0; }
  uint32_t contentHash() const { return hash_; }
  int styleCount() const { return 1; }
  void* getEpdFont(int) { return nullptr; }
};
