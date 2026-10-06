#pragma once

#include <cstdint>

#include "activities/Activity.h"

namespace clock_sync_power {
constexpr uint32_t IDLE_TIMEOUT_MS = 5u * 60u * 1000u;

inline bool idleExitDue(const uint32_t now, const bool waiting, const bool radioAlive, const bool interaction,
                        uint32_t& idleSince, bool& timerStarted) {
  if (!waiting || !radioAlive) {
    timerStarted = false;
    return false;
  }
  if (!timerStarted || interaction) {
    idleSince = now;
    timerStarted = true;
    return false;
  }
  return now - idleSince >= IDLE_TIMEOUT_MS;
}
}  // namespace clock_sync_power

// Manual NTP resync action. Runs a forced sync (bypassing the once-per-device debounce),
// reports success/failure, then waits for Back. If WiFi is not connected yet, it reuses the
// normal WiFi selection flow first.
class ClockSyncActivity final : public Activity {
 public:
  explicit ClockSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClockSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { SYNCING, SUCCESS, NO_WIFI, FAILED, TIMEZONE_FAILED };
  State state = SYNCING;
  char syncedTime[16] = {0};
  bool runtimeStarted = false;
  bool shouldTearDownWifiOnExit = false;
  uint32_t idleSince = 0;
  bool idleTimerStarted = false;

  void runSync();
  void renderUgly();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  bool idleExitDue(unsigned long now, bool interaction);
};
