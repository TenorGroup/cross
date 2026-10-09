#include "BootTrial.h"

#if FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)

#include <Logging.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <atomic>

// Keeps a trial image PENDING_VERIFY through initArduino(), which would otherwise accept it before
// setup() runs. setup() accepts it after the first paint and touch self-check.
extern "C" bool verifyRollbackLater() { return true; }

namespace boot_trial {
namespace {
// A trial boot checks a simple Home tab before opening any book.
constexpr uint64_t WATCHDOG_US = 30ULL * 1000 * 1000;
esp_timer_handle_t watchdog = nullptr;
std::atomic<const char*> failureReason{"boot trial: startup did not complete within 30 s"};

void onWatchdog(void*) {
  // The callback may already be queued when acceptance stops the timer.
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }
  esp_system_abort(failureReason.load(std::memory_order_relaxed));
}
}  // namespace

bool begin() {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
      state != ESP_OTA_IMG_PENDING_VERIFY) {
    return false;
  }
  esp_timer_create_args_t args = {};
  args.callback = onWatchdog;
  args.name = "boot_trial";
  if (esp_timer_create(&args, &watchdog) != ESP_OK) {
    esp_system_abort("boot trial: watchdog creation failed");
  }
  if (esp_timer_start_once(watchdog, WATCHDOG_US) != ESP_OK) {
    esp_timer_delete(watchdog);
    watchdog = nullptr;
    esp_system_abort("boot trial: watchdog start failed");
  }
  LOG_INF("OTA", "Boot on trial, watchdog armed");
  return true;
}

void passed(const bool touchReady) {
  if (!watchdog) return;
  if (!touchReady) {
    failureReason.store("boot trial: touch controller unavailable", std::memory_order_relaxed);
    LOG_ERR("OTA", "Boot self-check: no touch controller, left on trial");
    return;
  }
  const esp_err_t verified = esp_ota_mark_app_valid_cancel_rollback();
  if (verified != ESP_OK) {
    failureReason.store("boot trial: image acceptance failed", std::memory_order_relaxed);
    LOG_ERR("OTA", "Boot self-check failed: %s", esp_err_to_name(verified));
    return;
  }
  esp_timer_stop(watchdog);
  esp_timer_delete(watchdog);
  watchdog = nullptr;
  LOG_INF("OTA", "Boot self-check complete: %s", esp_err_to_name(verified));
}

}  // namespace boot_trial

#endif
