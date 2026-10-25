#pragma once
// Measurement builds of the X3 only: a watchdog for a main loop or a render task that stops for good (v1.1.0 journeys,
// two hangs with no panic and no restart). Past the limit it writes where both tasks stand to RTC memory (COLDLOG
// prints it after the restart), prints it to the cable, and aborts: the panic stores a core dump of every task.
#include <cstdint>

namespace hangwatch {

constexpr uint32_t kLimitMs = 20000;
enum : uint8_t { kLoop = 1, kRender = 2 };

// Which tasks are stuck: the loop when no pass began within the limit, the render task when the paint it is on
// began that long ago (renderSinceMs 0: it waits for work). Wrap-safe on millis().
inline uint8_t stuck(const uint32_t nowMs, const uint32_t loopSeenMs, const uint32_t renderSinceMs,
                     const uint32_t limitMs = kLimitMs) {
  uint8_t s = 0;
  if (nowMs - loopSeenMs >= limitMs) s |= kLoop;
  if (renderSinceMs != 0 && nowMs - renderSinceMs >= limitMs) s |= kRender;
  return s;
}

#if defined(TENOR_PRESS_PROBE) && defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3 && !defined(SIMULATOR)
// Each loop pass (the first one names the loop task and starts the watchdog). From another task it does nothing.
void loopAlive();
// The render task, around each paint.
void renderBusy(bool busy);
// COLDLOG: the last trip, if any.
void dump();
#else
inline void loopAlive() {}
inline void renderBusy(bool) {}
inline void dump() {}
#endif

}  // namespace hangwatch
