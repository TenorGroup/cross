#include <cassert>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "Logging.h"
#include "BootTrial.h"

struct FakeTimer {
  esp_timer_create_args_t args;
  bool armed = false;
  uint64_t due = 0;
};
struct Abort : std::runtime_error { using std::runtime_error::runtime_error; };
FakeTimer timer;
esp_partition_t partition;
esp_ota_img_states_t state = ESP_OTA_IMG_PENDING_VERIFY;
esp_err_t queryResult = ESP_OK, createResult = ESP_OK, startResult = ESP_OK, markResult = ESP_OK;
int created = 0, deleted = 0, marked = 0, paints = 0, readers = 0;
bool acceptedBeforePaint = false;
uint64_t now = 0;
const esp_partition_t* esp_ota_get_running_partition() { return &partition; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t* result) {
  *result = state;
  return queryResult;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback() {
  ++marked;
  acceptedBeforePaint = paints == 0;
  if (markResult == ESP_OK) state = ESP_OTA_IMG_VALID;
  return markResult;
}
const char* esp_err_to_name(esp_err_t e) { return e == ESP_OK ? "OK" : "FAIL"; }
esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* handle) {
  if (createResult != ESP_OK) return createResult;
  ++created;
  timer.args = *args;
  *handle = &timer;
  return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t duration) {
  if (startResult != ESP_OK) return startResult;
  handle->armed = true;
  handle->due = now + duration;
  return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t handle) { handle->armed = false; return ESP_OK; }
esp_err_t esp_timer_delete(esp_timer_handle_t) { ++deleted; return ESP_OK; }
[[noreturn]] void esp_system_abort(const char* reason) { throw Abort(reason); }
void advance(uint64_t us) {
  now += us;
  if (timer.armed && now >= timer.due) {
    timer.armed = false;
    timer.args.callback(timer.args.arg);
  }
}

enum class BootResume { Splash, Silent, SplashlessWake };
enum class HomeMenuItem { NONE, RECENT_CONTINUE, SETTINGS_MENU };
constexpr unsigned SILENT_REBOOT_TARGET_READER = 1, SILENT_REBOOT_TARGET_SETTINGS = 2;
bool recoveryFirmwareMode = false, rebootedFromPanic = false, updateBoot = false, wakeToBook = false;
bool needsWakeRefresh = false;
BootResume resume = BootResume::Splash;
unsigned snapshotTarget = 0;
HomeMenuItem snapshotHomeMenu = HomeMenuItem::NONE;
std::string wakeBook = "/huge.epub";
struct { std::string openEpubPath = "/huge.epub"; } APP_STATE;
int renderer = 0, mappedInputManager = 0, otaBoot = 0;
struct SdFirmwareUpdateActivity { template<class... T> explicit SdFirmwareUpdateActivity(T...) {} };
struct OtaUpdateActivity { template<class... T> explicit OtaUpdateActivity(T...) {} };
template<class T, class... A> std::unique_ptr<T> makeUniqueNoThrow(A... args) {
  return std::make_unique<T>(args...);
}
struct {
  bool touch = true;
  bool hasTouch() const { return touch; }
} gpio;
struct {
  HomeMenuItem home = HomeMenuItem::NONE;
  template<class T> void replaceActivity(std::unique_ptr<T>) {}
  void goToCrashReport() {}
  void goToSettings() {}
  void goHome(HomeMenuItem tab = HomeMenuItem::NONE, bool = false) { home = tab; }
  void goToReader(const std::string&) { ++readers; advance(35000000); }
  void requestUpdateAndWait() { ++paints; advance(1000000); }
} activityManager;

void runBoot() {
#include "adapter.inc"
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string name = argv[1];
  if (name == "valid") state = ESP_OTA_IMG_VALID;
  if (name == "new") state = ESP_OTA_IMG_NEW;
  if (name == "aborted") state = ESP_OTA_IMG_ABORTED;
  if (name == "query-error") queryResult = ESP_FAIL;
  if (name == "create-error") createResult = ESP_FAIL;
  if (name == "start-error") startResult = ESP_FAIL;
  if (name == "no-touch") gpio.touch = false;
  if (name == "mark-error") markResult = ESP_FAIL;
  if (name == "silent-large-book") { resume = BootResume::Silent; snapshotTarget = SILENT_REBOOT_TARGET_READER; }
  if (name == "wake-large-book") { resume = BootResume::SplashlessWake; wakeToBook = true; }
  if (name == "valid-wake-book") { state = ESP_OTA_IMG_VALID; wakeToBook = true; }
  if (name == "recovery") recoveryFirmwareMode = true;

  std::string abortReason;
  try {
    if (name == "hung-paint" || name == "valid-race") {
      boot_trial::begin();
      if (name == "valid-race") state = ESP_OTA_IMG_VALID;
    } else {
      runBoot();
    }
    advance(35000000);
  } catch (const Abort& e) { abortReason = e.what(); }

  bool ok = false;
  if (name == "create-error" || name == "start-error")
    ok = !abortReason.empty() && marked == 0 && (name != "start-error" || deleted == 1);
  else if (name == "no-touch")
    ok = marked == 0 && state == ESP_OTA_IMG_PENDING_VERIFY && abortReason.find("touch") != std::string::npos;
  else if (name == "mark-error")
    ok = marked == 1 && state == ESP_OTA_IMG_PENDING_VERIFY && abortReason.find("accept") != std::string::npos;
  else if (name == "hung-paint")
    ok = marked == 0 && state == ESP_OTA_IMG_PENDING_VERIFY && !abortReason.empty();
  else if (name == "valid-race")
    ok = abortReason.empty() && marked == 0;
  else if (name == "silent-large-book" || name == "wake-large-book")
    ok = abortReason.empty() && readers == 0 && marked == 1 && !acceptedBeforePaint &&
         activityManager.home == HomeMenuItem::SETTINGS_MENU;
  else if (name == "valid-wake-book")
    ok = abortReason.empty() && readers == 1 && marked == 0 && created == 0;
  else if (name == "valid" || name == "new" || name == "aborted" || name == "query-error")
    ok = abortReason.empty() && marked == 0 && created == 0;
  else if (name == "pass" || name == "recovery")
    ok = abortReason.empty() && marked == 1 && state == ESP_OTA_IMG_VALID && deleted == 1 && paints == 1 &&
         !acceptedBeforePaint;
  std::printf("%s %s created=%d deleted=%d marked=%d paints=%d readers=%d state=%d reason=%s\n",
              ok ? "PASS" : "FAIL", name.c_str(), created, deleted, marked, paints, readers,
              static_cast<int>(state), abortReason.c_str());
  return ok ? 0 : 1;
}
