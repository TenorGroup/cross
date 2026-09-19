#include <cstdio>
#include <string>
#include "BlePageTurnerRuntime.h"
#include "FileTransferState.h"
#include "GfxRenderer.h"
#include "FontCacheManager.h"
#include "BleKeyboardHost.h"
#include "HalPowerManager.h"
#include "freertos/task.h"
static unsigned long testNow = 10000;
static unsigned long lastActivityTime = 0;
unsigned long millis() { return testNow; }
void delay(unsigned long amount) { testNow += amount; }
void yield() {}
static HalPowerManager powerManager;
static struct { bool skipLoopDelay() const { return false; } } activityManager;
static struct { bool rawInputActive() const { return false; } } gpio;
static auto& bleHid = freeink::BleKeyboardHost::getInstance();
void runMainPowerTail() {
#include "main_power.inc"
}
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("FAIL line %d: %s\n", __LINE__, #expr); ++failures; } } while (0)
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string test = argv[1];
  GfxRenderer renderer;
  FontCacheManager cache;
  renderer.setFontCacheManager(&cache);
  HalMemory::internalHeap = {100000, 249216, 100000, 65536};
  auto& host = freeink::BleKeyboardHost::getInstance();
  if (test == "queued_start_cancelled") {
    CHECK(freeink::ble::beginAsync(renderer));
    CHECK(power_test::locks > 0);
    CHECK(!freeink::ble::suspendForTransition());
    CHECK(host.endCalls == 0);
    scheduler::run();
    CHECK(host.beginCalls == 0);
    CHECK(freeink::ble::suspendForTransition());
    CHECK(!host.running);
    CHECK(power_test::locks == 0);
  } else if (test == "queued_idle_stop_keeps_reason") {
    CHECK(freeink::ble::beginAsync(renderer));
    CHECK(!freeink::ble::stopForIdle());
    CHECK(freeink::ble::idleStopped());
    scheduler::run();
    CHECK(host.beginCalls == 0);
    CHECK(freeink::ble::suspendForTransition());
    CHECK(freeink::ble::idleStopped());
    CHECK(power_test::locks == 0);
  } else if (test == "suspend_during_sdk_begin") {
    host.beginHook = [&] {
      CHECK(!freeink::ble::suspendForTransition());
      CHECK(host.endCalls == 0);
      CHECK(power_test::locks > 0);
    };
    CHECK(freeink::ble::beginAsync(renderer));
    scheduler::run();
    CHECK(host.beginHadPower);
    CHECK(!host.endDuringBegin);
    CHECK(freeink::ble::suspendForTransition());
    CHECK(host.endCalls > 0);
    CHECK(host.endHadPower);
    CHECK(!host.running);
    CHECK(power_test::locks == 0);
  } else if (test == "filetransfer_during_sdk_begin") {
    host.beginHook = [&] {
      CHECK(!filetransfer::acquire());
      CHECK(filetransfer::isActive());
      CHECK(host.endCalls == 0);
    };
    CHECK(freeink::ble::beginAsync(renderer));
    scheduler::run();
    CHECK(!host.endDuringBegin);
    CHECK(freeink::ble::suspendForTransition());
    CHECK(!host.running);
    CHECK(power_test::locks == 0);
    filetransfer::release();
    CHECK(!filetransfer::isActive());
  } else if (test == "power_through_pending_teardown") {
    CHECK(freeink::ble::beginAsync(renderer));
    scheduler::run();
    CHECK(host.beginHadPower);
    CHECK(power_test::locks > 0);
    host.endResult = false;
    CHECK(!freeink::ble::suspendForTransition());
    CHECK(host.endHadPower);
    CHECK(power_test::locks > 0);
    host.endResult = true;
    CHECK(freeink::ble::suspendForTransition());
    CHECK(power_test::locks == 0);
  } else if (test == "sync_begin_power") {
    CHECK(freeink::ble::begin(renderer));
    CHECK(host.beginHadPower);
    CHECK(power_test::locks > 0);
    CHECK(freeink::ble::suspendForTransition());
    CHECK(host.endHadPower);
    CHECK(power_test::locks == 0);
  } else if (test == "double_start_owns_one_worker") {
    CHECK(freeink::ble::beginAsync(renderer));
    CHECK(!freeink::ble::beginAsync(renderer));
    CHECK(!freeink::ble::begin(renderer));
    CHECK(scheduler::creates == 1);
    CHECK(host.beginCalls == 0);
    scheduler::run();
    CHECK(host.beginCalls == 1);
    CHECK(freeink::ble::suspendForTransition());
    CHECK(power_test::locks == 0);
  } else if (test == "main_power_owns_queued_init_and_teardown") {
    power_test::saving = true;
    CHECK(freeink::ble::beginAsync(renderer));
    CHECK(!power_test::saving);
    runMainPowerTail();
    CHECK(!power_test::saving);
    CHECK(power_test::enableRequests == 0);
    host.beginHook = [&] {
      runMainPowerTail();
      CHECK(!power_test::saving);
      CHECK(power_test::enableRequests == 0);
    };
    scheduler::run();
    runMainPowerTail();
    CHECK(!power_test::saving);
    host.endResult = false;
    CHECK(!freeink::ble::suspendForTransition());
    runMainPowerTail();
    CHECK(!power_test::saving);
    CHECK(power_test::enableRequests == 0);
    host.endResult = true;
    CHECK(freeink::ble::suspendForTransition());
    runMainPowerTail();
    CHECK(power_test::saving);
    CHECK(power_test::enableRequests == 1);
  } else if (test == "worker_creation_failure_releases_owner") {
    scheduler::createSucceeds = false;
    CHECK(!freeink::ble::beginAsync(renderer));
    CHECK(power_test::locks == 0);
    CHECK(freeink::ble::suspendForTransition());
    scheduler::createSucceeds = true;
    CHECK(freeink::ble::beginAsync(renderer));
    scheduler::run();
    CHECK(host.beginCalls == 1);
    CHECK(freeink::ble::suspendForTransition());
  } else if (test == "sdk_begin_failure_releases_power") {
    host.beginResult = false;
    CHECK(freeink::ble::beginAsync(renderer));
    scheduler::run();
    CHECK(host.beginHadPower);
    CHECK(freeink::ble::suspendForTransition());
    CHECK(power_test::locks == 0);
  } else if (test == "postinit_headroom_pending_teardown") {
    host.changeHeapOnBegin = true;
    host.heapAfterBegin = {40000, 249216, 10000, 32767};
    host.endResult = false;
    CHECK(freeink::ble::beginAsync(renderer));
    scheduler::run();
    CHECK(host.stopping);
    CHECK(host.beginHadPower);
    CHECK(host.endHadPower);
    CHECK(power_test::locks > 0);
    CHECK(!freeink::ble::beginAsync(renderer));
    CHECK(!freeink::ble::suspendForTransition());
    host.endResult = true;
    CHECK(freeink::ble::suspendForTransition());
    CHECK(power_test::locks == 0);
  } else return 2;
  std::printf("%s %s (%d failures)\n", failures ? "RED" : "GREEN", argv[1], failures);
  return failures ? 1 : 0;
}
