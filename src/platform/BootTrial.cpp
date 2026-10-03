#include "BootTrial.h"

#if FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)

#include <Logging.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>

// Keeps a trial image PENDING_VERIFY through initArduino(), which would otherwise accept it before
// setup() runs. setup() accepts it after the first paint (esp_ota_mark_app_valid_cancel_rollback).
extern "C" bool verifyRollbackLater() { return true; }

namespace boot_trial {
namespace {
// Only a hang should reach this: the slowest first frame measured is ~9 s (a 5,000-chapter book
// opened for the first time, on the slower X3), and a trial boot after an install goes to Home.
constexpr uint64_t WATCHDOG_US = 30ULL * 1000 * 1000;
esp_timer_handle_t watchdog = nullptr;

void onWatchdog(void*) { esp_system_abort("boot trial: no first frame within 30 s"); }
}  // namespace

void begin() {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
      state != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }
  esp_timer_create_args_t args = {};
  args.callback = onWatchdog;
  args.name = "boot_trial";
  const bool armed =
      esp_timer_create(&args, &watchdog) == ESP_OK && esp_timer_start_once(watchdog, WATCHDOG_US) == ESP_OK;
  LOG_INF("OTA", "Boot on trial, watchdog %s", armed ? "armed" : "FAILED");
}

void passed() {
  if (!watchdog) return;
  esp_timer_stop(watchdog);
  esp_timer_delete(watchdog);
  watchdog = nullptr;
}

}  // namespace boot_trial

#endif
