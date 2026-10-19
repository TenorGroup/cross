#pragma once
#include <cstdint>
#include <cstdlib>
class EpdFontFamily;
class SdCardFont {
  uint32_t hash_ = 0;
 public:
  inline static int alive = 0;
  inline static bool builtinLayoutMatches = false;
  inline static bool regularStyle = true;  // false: a file holding only bold or italic
  SdCardFont() { ++alive; }
  ~SdCardFont() { --alive; }
  bool load(const char* path) { hash_ = std::strtoul(path, nullptr, 10); return hash_ != 0; }
  bool matchesBuiltinLayout(const EpdFontFamily&) const { return builtinLayoutMatches; }
  uint32_t contentHash() const { return hash_; }
  int styleCount() const { return 1; }
  bool hasStyle(int style) const { return style != 0 || regularStyle; }
  void* getEpdFont(int) { return nullptr; }
};
