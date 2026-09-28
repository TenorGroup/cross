#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "BattShown.h"
#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;

  mutable int _batteryCachedPercent = 0;         // Last read battery percentage (0-100)
  mutable unsigned long _batteryLastPollMs = 0;  // Timestamp of last battery read in milliseconds
  TaskHandle_t _gaugeTask = nullptr;             // The loop task, the only one that reads an I2C gauge

  // Display-smoothing state for getDisplayedBatteryPercentage(). That accessor is the only
  // writer: every current caller runs on the render task (the same task that already draws
  // the status bar and About screen from cached gauge values), so this needs no lock. A new
  // caller from another task must not touch this directly.
  mutable battshown::State _shownState;

  uint16_t normalSpeedLocks = 0;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect the lock count and CPU-frequency transition

  mutable unsigned long _gaugeDiagLastPollMs = 0;  // Timestamp of the last extra-register read

 public:
  // Extra BQ27220 registers for the About screen's diagnostic rows (X3 only). Deliberately
  // raw, unsmoothed numbers, so a photo of them can be compared against the TRM directly.
  // pollGauge() (loop task) is the only writer; every field reads 0/false until the first
  // successful poll.
  struct GaugeDiagnostics {
    bool valid = false;
    uint16_t millivolts = 0;
    int16_t averageCurrentMa = 0;
    uint16_t remainingCapacityMah = 0;
    uint16_t fullChargeCapacityMah = 0;
    uint16_t designCapacityMah = 0;
    uint16_t stateOfChargePercent = 0;
    uint16_t stateOfHealthPercent = 0;
    uint16_t cycleCount = 0;
    uint16_t statusFlags = 0;  // BatteryStatus() bit field
  };
  static constexpr unsigned long GAUGE_DIAG_POLL_MS = 30000;  // ms; well under any flash/heap budget

 private:
  mutable GaugeDiagnostics _gaugeDiagnostics;  // Cache; only pollGauge() (loop task) writes it
  void pollGaugeDiagnostics() const;

 public:
#if BOARD_HAS_PSRAM
  static constexpr int LOW_POWER_FREQ = 80;  // MHz
#else
  static constexpr int LOW_POWER_FREQ = 10;  // MHz
#endif
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;  // ms
  static constexpr unsigned long BATTERY_POLL_MS = 1500;       // ms

  void begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // Setup wake up GPIO and enter deep sleep
  // Should be called inside main loop() to respect active normal-speed locks
  // preserveClock keeps the C3 X4 supply latched for its RTC-domain system clock.
  // X3 and other boards retain their normal rail policy.
  void startDeepSleep(HalGPIO& gpio, bool preserveClock = false) const;

  // Get battery percentage (range 0-100). On a board with an I2C gauge only the loop
  // task reads the gauge; other tasks (the render task) get the loop's last reading.
  // This is the RAW gauge/ADC value: safety logic (low-battery sleep, charging decisions)
  // must call this, never getDisplayedBatteryPercentage().
  uint16_t getBatteryPercentage() const;

  // The single accessor every screen uses to show the battery percentage. Wraps
  // getBatteryPercentage() with battshown::next() (BattShown.h): the raw value can jump
  // (BQ27220 EDV hard-corrections, TRM SLUUBD4A section 1.1.1), so
  // what the user sees is smoothed while safety logic keeps reading the raw number.
  uint16_t getDisplayedBatteryPercentage() const;
  // The one answer to "is the gauge a BQ27220": the capacity load, the diagnostics read and
  // the About rows all ask it. Other gauges (CW2017, AXP2101) have other registers.
  static bool hasBq27220Gauge();

  // Cached copy of the extra BQ27220 registers, refreshed by pollGauge() at most every
  // GAUGE_DIAG_POLL_MS. `.valid` is false on boards with no gauge, or before the first poll.
  const GaugeDiagnostics& gaugeDiagnostics() const { return _gaugeDiagnostics; }

  // The main loop's battery poll: refreshes the gauge reading the render task draws. A board
  // without a gauge reads its ADC divider only when the battery is drawn, because the loop
  // runs every 10 ms and the button ladder timer owns that ADC unit.
  void pollGauge() const;

  // True on the only task allowed to talk to an I2C battery gauge (the loop task, which also
  // runs setup()). Every gauge read asks here, the charging check in HalGPIO included.
  bool mayReadGauge() const;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for example when running a task that needs
  // full performance. When the Lock instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;

   public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};
