#pragma once
#include <cassert>
extern int uiTestLockDepth;
class FontCacheManager {
 public:
  int clearCount = 0;
  template <typename T> void setFontDecompressor(T*) {}
  void clearCache() { assert(uiTestLockDepth > 0); ++clearCount; }
};
