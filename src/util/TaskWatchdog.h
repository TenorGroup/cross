#pragma once

#include <esp_err.h>
#include <esp_task_wdt.h>

#include "platform/HangWatch.h"

inline void resetTaskWatchdogIfSubscribed() {
  hangwatch::loopAlive();  // a long pass that feeds this watchdog feeds the measurement build's too
  if (esp_task_wdt_status(nullptr) == ESP_OK) {
    esp_task_wdt_reset();
  }
}
