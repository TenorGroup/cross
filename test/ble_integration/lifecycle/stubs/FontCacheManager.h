#pragma once

#include "activities/RenderLock.h"

namespace ble_runtime_test {
inline unsigned cacheReleaseCalls = 0;
inline bool cacheReleaseHeldLock = false;
}  // namespace ble_runtime_test

class FontCacheManager {
 public:
  void releaseSdFontCaches() {
    ++ble_runtime_test::cacheReleaseCalls;
    ble_runtime_test::cacheReleaseHeldLock = RenderLock::peek();
  }
};
