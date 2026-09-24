#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;

  mutable int _batteryCachedPercent = 0;         // Last read battery percentage (0-100)
  mutable unsigned long _batteryLastPollMs = 0;  // Timestamp of last battery read in milliseconds
  TaskHandle_t _gaugeTask = nullptr;             // The loop task, the only one that reads an I2C gauge

  uint16_t normalSpeedLocks = 0;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect the lock count and CPU-frequency transition

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
  uint16_t getBatteryPercentage() const;

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
