#pragma once
#include <Arduino.h>
#include <Memory.h>
#include "TenorInk.h"

namespace tenorink {
template <typename T, typename... Args>
auto internal(size_t bytes, Args&&... args) {
#if defined(ESP_PLATFORM) && !defined(BOARD_HAS_PSRAM)
  if (bytes >= 4096 && !homeHeapFits(ESP.getFreeHeap(), ESP.getMaxAllocHeap(), bytes))
    return decltype(makeUniqueNoThrow<T>(std::forward<Args>(args)...)){};
#endif
  auto p = makeUniqueNoThrow<T>(std::forward<Args>(args)...);
#if defined(ESP_PLATFORM) && !defined(BOARD_HAS_PSRAM)
  if (p && bytes >= 4096 && !homeHeapFits(ESP.getFreeHeap(), ESP.getMaxAllocHeap())) p.reset();
#endif
  return p;
}
}
