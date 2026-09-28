#include <Arduino.h>
#include <BoardConfig.h>
#include <FileTransferBackLatch.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "FirmwareFlasher.h"
#include "HttpDownloader.h"
#include "OtaUpdater.h"

// The production OTA install (activity step, updater, Back sampler) runs against a
// fake transport and a fake update slot; a physical Back tap arrives through the
// same ADC ladder sample the sampler reads on the device.
using namespace std::chrono;
static const auto started = steady_clock::now();
unsigned long millis() { return duration_cast<milliseconds>(steady_clock::now() - started).count(); }
static std::atomic<int> rawKey{-1};
BoardConfig::Board BoardConfig::ACTIVE;
struct FakeTask {
  std::thread thread;
};
static std::vector<std::unique_ptr<FakeTask>> tasks;
struct TaskExit {};
void vTaskDelay(TickType_t ms) { std::this_thread::sleep_for(milliseconds(ms)); }
void vTaskDelete(TaskHandle_t) { throw TaskExit{}; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 1176; }
int xTaskCreate(void (*fn)(void*), const char*, uint32_t, void* context, unsigned, TaskHandle_t* handle) {
  auto task = std::make_unique<FakeTask>();
  *handle = task.get();
  task->thread = std::thread([=] {
    try {
      fn(context);
    } catch (const TaskExit&) {
    }
  });
  tasks.push_back(std::move(task));
  return pdPASS;
}
void HalGPIO::sampleButtonAdc(InputManager::ButtonAdcSample& first, InputManager::ButtonAdcSample& second) {
  const int key = rawKey.load();
  first = {1, key == 0 ? 3610 : 4095, key == 0 ? 0 : -1};
  second = {2, 4095, -1};
}
HalGPIO gpio;
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

// Update slot.
OtaCalls otaCalls;
static const esp_partition_t slot{1 << 20};
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &slot; }
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t* handle) {
  ++otaCalls.begin;
  *handle = 1;
  return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t len) {
  otaCalls.written += len;
  return ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t) {
  ++otaCalls.abort;
  return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t) {
  ++otaCalls.end;
  return ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) {
  ++otaCalls.setBoot;
  return ESP_OK;
}
uint16_t firmware_flash::runningPartitionChipId() { return 5; }

// A C3 image carrying this board's tag.
static std::vector<uint8_t> image;
const char* fixtureTag = "v9.9.9";
size_t ReleaseJsonParser::getFirmwareSize() const { return image.size(); }
static size_t stallAt = SIZE_MAX;
static size_t shortBy = 0;  // the server ends the body early but cleanly
static unsigned long transferEndedAt = 0;
static int fetches = 0;

// Stands in for the TLS transfer: 1 KB records every 4 ms, or silence from stallAt on
// until the 10 s read timeout. Like the downloader's wait, every pass pumps progress and
// then honours the cancel flag.
static bool fakeTransfer(const std::string& url, const HttpDownloader::DataCallback& onData,
                         const HttpDownloader::ProgressCallback& progress, const bool* cancelFlag,
                         HttpDownloader::TransferStats* stats) {
  ++fetches;
  const unsigned long started = millis();
  if (stats) *stats = {200, true, 0, 0, 0, 0, 90000, 50000, 50000};
  if (url.find("/firmware/tenor-cross") == std::string::npos) return true;  // the manifest
  size_t sent = 0;
  unsigned long lastByte = millis();
  bool ok = true;
  while (sent < image.size() - shortBy) {
    if (progress) progress(sent, image.size());
    if (cancelFlag && *cancelFlag) {
      ok = false;
      break;
    }
    if (sent >= stallAt) {
      if (millis() - lastByte >= 10000) {
        ok = false;
        break;
      }
      vTaskDelay(2);
      continue;
    }
    vTaskDelay(4);
    const size_t n = std::min<size_t>(1024, image.size() - sent);
    if (!onData(image.data() + sent, n)) {
      ok = false;
      break;
    }
    sent += n;
    lastByte = millis();
  }
  transferEndedAt = millis();
  if (stats) {
    stats->bytes = sent;
    stats->total = image.size();
    stats->elapsedMs = transferEndedAt - started;
    stats->idleMs = transferEndedAt - lastByte;
  }
  return ok;
}
#if FETCH_TAKES_CANCEL
bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string&,
                              const std::string&, const char*, bool, ProgressCallback progress, bool* cancelFlag,
                              TransferStats* stats) {
  return fakeTransfer(url, onData, progress, cancelFlag, stats);
}
#else
bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string&,
                              const std::string&, const char*, bool) {
  return fakeTransfer(url, onData, nullptr, nullptr, nullptr);
}
#endif

// The activity around the production install step.
enum StrId { STR_FIRMWARE_WRONG_DEVICE };
const char* tr(StrId) { return "wrong device"; }
struct FakeFontCache {
  void releaseSdFontCaches() {}
};
struct FakeRenderer {
  FakeFontCache* getFontCacheManager() { return nullptr; }
};
struct MappedInputManager {
  enum class Button { Back };
  uint8_t backKey = 0;
  mutable int suppressedBackReleases = 0;
  uint8_t physicalBack() const { return backKey; }
  void suppressNextRelease(Button) const { ++suppressedBackReleases; }
};
struct OtaUpdateActivity;
// What the screen handed to the card log: the operation, whether it was written at once
// (after a screen already drawn) or deferred to the next frame, and the attempt itself.
struct Recorded {
  std::string op;
  bool now;
  ota_log::Attempt attempt;
};
struct RenderLock {
  explicit RenderLock(OtaUpdateActivity&) {}
};
struct OtaUpdateActivity {
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };
  State state = WAITING_CONFIRMATION;
  int finishes = 0;
  OtaUpdater updater;
  const char* failedDetail = nullptr;
  FileTransferBackLatch backLatch;
  FakeRenderer renderer;
  MappedInputManager mappedInput;
  void requestUpdate(bool = false) {}
  void requestUpdateAndWait() {}
  std::vector<Recorded> recorded;
  void finish() { ++finishes; }
  void recordAttempt(const char* op, bool now = false) { recorded.push_back({op, now, updater.lastAttempt()}); }
  void runUpdateInstall();
};
#include "production-install.inc"

void run(const std::string& name) {
  image.assign(128 * 1024, 0);
  image[0] = 0xE9;
  image[12] = 5;
  const char tag[] = "CROSSPOINT-BOARD-V1:x4;";
  std::memcpy(image.data() + 100, tag, sizeof(tag) - 1);
  OtaUpdateActivity activity;
  const auto one = [&](const char* op, bool now) -> const ota_log::Attempt& {
    require(activity.recorded.size() == 1, "the screen did not hand exactly one line to the card log");
    require(activity.recorded[0].op == op && activity.recorded[0].now == now, "wrong operation or timing in the log");
    return activity.recorded[0].attempt;
  };
  const auto is = [](const char* a, const char* b) { return std::strcmp(a, b) == 0; };
  if (name == "manifest-outside-firmware-dir-refused") {
    activity.updater.setDryRun("https://example.com/firmware/x.json");
    require(activity.updater.checkForUpdate() == OtaUpdater::HTTP_ERROR,
            "a manifest outside the firmware directory was read");
    require(fetches == 0 && is(activity.updater.lastAttempt().step, "url"), "the refused manifest was fetched");
    return;
  }
  if (name == "dry-run-keeps-boot-slot" || name == "older-refused-without-dry-run" ||
      name == "dry-run-short-image-released") {
    fixtureTag = "v0.0.1";  // older than any running firmware
    if (name == "dry-run-short-image-released") shortBy = 1024;
    if (name != "older-refused-without-dry-run")
      activity.updater.setDryRun("https://cross.tenor.vn/firmware/test/dry-run.json");
    require(activity.updater.checkForUpdate() == OtaUpdater::OK, "the test manifest was not offered");
    require(!activity.updater.isUpdateNewer(), "fixture should be older");
    if (name == "older-refused-without-dry-run") {
      require(activity.updater.installUpdate() == OtaUpdater::UPDATE_OLDER_ERROR && otaCalls.begin == 0,
              "a real install took an older image");
      require(is(activity.updater.lastAttempt().step, "version"), "the refusal was not recorded as the version step");
      return;
    }
    activity.runUpdateInstall();
    const auto& a = activity.updater.lastAttempt();
    std::cout << name << " step=" << a.step << " err=" << a.err << " end=" << otaCalls.end
              << " setBoot=" << otaCalls.setBoot << " abort=" << otaCalls.abort << '\n';
    require(otaCalls.setBoot == 0, "the dry run switched the boot slot");
    require(
        activity.finishes == 0 && activity.recorded.empty() && activity.state == OtaUpdateActivity::UPDATE_IN_PROGRESS,
        "the dry run went on to the install result screen");
    if (name == "dry-run-keeps-boot-slot") {
      require(
          a.ok && is(a.step, "done") && otaCalls.written == image.size() && otaCalls.end == 1 && otaCalls.abort == 0,
          "the dry run did not write, verify and close the whole image");
    } else {
      require(!a.ok && is(a.step, "verify") && is(a.err, "INTEGRITY_ERROR") && otaCalls.end == 0 && otaCalls.abort == 1,
              "a short image was not released through the OTA API");
    }
    return;
  }
  require(activity.updater.checkForUpdate() == OtaUpdater::OK && activity.updater.isUpdateNewer(),
          "fixture manifest was not offered");
  if (name == "no-back-installs") {
    activity.runUpdateInstall();
    require(activity.state == OtaUpdateActivity::SHUTTING_DOWN, "an untouched download did not install");
    require(otaCalls.written == image.size() && otaCalls.end == 1 && otaCalls.setBoot == 1 && otaCalls.abort == 0,
            "an untouched download did not switch the boot slot");
    require(!activity.backLatch.active(), "Back sampler kept running after the install");
    const auto& a = one("install", true);
    require(a.ok && is(a.step, "done") && a.bytes == image.size() && a.total == image.size() && a.rssi == -67 &&
                a.xferMs > 0 && is(a.wd, "none"),
            "the success line lacks the bytes, time or signal");
    return;
  }
  if (name == "stall-ends-on-idle-deadline") {
    stallAt = 16 * 1024;
    activity.runUpdateInstall();
    require(activity.state == OtaUpdateActivity::FAILED && otaCalls.abort == 1 && otaCalls.setBoot == 0,
            "a stalled download did not fail cleanly");
    const auto& a = one("install", false);
    std::cout << name << " step=" << a.step << " err=" << a.err << " bytes=" << a.bytes << " idle=" << a.idleMs
              << " wd=" << a.wd << '\n';
    require(!a.ok && is(a.step, "download") && is(a.err, "HTTP_ERROR") && a.http == 200 && a.bytes == stallAt &&
                a.idleMs >= 10000 && is(a.wd, "idle") && a.heap == 90000 && a.largestMin == 50000,
            "the failure line does not say the idle deadline ended the download");
    return;
  }
  if (name == "back-mid-download" || name == "back-stalled-download") {
    if (name == "back-stalled-download") stallAt = 16 * 1024;
    unsigned long releasedAt = 0;
    std::thread pulse([&] {
      vTaskDelay(100);
      rawKey = 0;
      vTaskDelay(60);
      rawKey = -1;
      releasedAt = millis();
    });
    activity.runUpdateInstall();
    pulse.join();
    std::cout << name << " written=" << otaCalls.written << " of=" << image.size()
              << " ended_after_release_ms=" << static_cast<long>(transferEndedAt - releasedAt) << '\n';
    require(activity.finishes == 1 && activity.state == OtaUpdateActivity::UPDATE_IN_PROGRESS,
            "Back during the download did not cancel it and leave");
    require(activity.mappedInput.suppressedBackReleases == 1,
            "the queued Back release would close the parent screen too");
    require(otaCalls.written < image.size(), "the download ran to its end");
    require(otaCalls.abort == 1 && otaCalls.end == 0 && otaCalls.setBoot == 0,
            "the cancelled slot was not released through the OTA API, or the boot slot changed");
    require(transferEndedAt - releasedAt < 600, "Back took longer than 0.6 s to end the download");
    require(!activity.backLatch.active(), "Back sampler kept running after the install");
    const auto& a = one("install", false);
    require(is(a.err, "CANCELLED_ERROR") && is(a.wd, "back") && is(a.step, "download"),
            "the cancelled download was not recorded as Back");
    return;
  }
  throw std::runtime_error("unknown case");
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  int result = 0;
  try {
    run(argv[1]);
    std::cout << "PASS " << argv[1] << '\n';
  } catch (const std::exception& e) {
    std::cerr << "FAIL " << argv[1] << ": " << e.what() << '\n';
    result = 1;
  }
  for (auto& task : tasks)
    if (task->thread.joinable()) task->thread.join();
  return result;
}
