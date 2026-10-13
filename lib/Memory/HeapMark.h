#pragma once

// Measurement builds only (TENOR_PRESS_PROBE): one line at a stage boundary with the heap now and
// its low mark since boot (grep "[HEAPMARK]"). Two marks in a row bracket the stage a low-mark drop
// falls in. Release builds expand HEAP_MARK to nothing: no call, no text.
#ifdef TENOR_PRESS_PROBE
#include <Arduino.h>
#include <Logging.h>

inline void heapMark(const char* at) {
  LOG_INF("HEAPMARK", "at=%s t=%lu free=%u largest=%u min=%u", at, millis(), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()));
}
#define HEAP_MARK(at) heapMark(at)
#else
#define HEAP_MARK(at) \
  do {                \
  } while (0)
#endif
