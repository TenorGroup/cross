#include <cstdarg>
#include <cstdio>
#include <optional>
#include <string>
#include "BlePageTurner.h"
#include "Runtime.h"
#include "FileTransferState.h"
#include "GfxRenderer.h"
#include "FontCacheManager.h"
#include "BleKeyboardHost.h"
#include "HalPowerManager.h"
#include "Logging.h"
#include "freertos/task.h"
static unsigned long testNow = 10000;
static unsigned long lastActivityTime = 0;
unsigned long millis() { return testNow; }
void delay(unsigned long amount) { testNow += amount; }
void yield() {}
static HalPowerManager powerManager;
static struct { bool rawInputActive() const { return false; } } gpio;
static const bool skipLoopDelay = false;
void runMainPowerTail() {
#include "main_power.inc"
}
// The firmware's host functions (src/BlePageTurnerHost.cpp), extracted unchanged.
namespace {
#include "host_glue.inc"
bool noDeliver(bleturner::Action) { return false; }
bool yieldNow() { return true; }
void noRestart() {}
void toLogs(bool, const char* format, va_list args) {
  char line[256];
  std::vsnprintf(line, sizeof line, format, args);
  ble_runtime_test::logs.emplace_back(line);
}
const bleturner::Host host{bleHeap, bleReleaseCaches, noDeliver, yieldNow, bleFileTransfer,
                           noRestart, bleHoldFullSpeed, toLogs, nullptr};
bleturner::Config config;
}  // namespace
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("FAIL line %d: %s\n", __LINE__, #expr); ++failures; } } while (0)
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string test = argv[1];
  GfxRenderer renderer;
  FontCacheManager cache;
  renderer.setFontCacheManager(&cache);
  bleRenderer = &renderer;
  config.enabled = 1;
  bleturner::begin(host, config);
  HalMemory::internalHeap = {100000, 249216, 100000, 65536};
  auto& host = freeink::BleKeyboardHost::getInstance();
  if (test == "queued_start_cancelled") {
    CHECK(bleturner::detail::startAsync());
    CHECK(power_test::locks > 0);
    CHECK(!bleturner::beforeScreenChange());
    CHECK(host.endCalls == 0);
    scheduler::run();
    CHECK(host.beginCalls == 0);
    CHECK(bleturner::beforeScreenChange());
    CHECK(!host.running);
    CHECK(power_test::locks == 0);
  } else if (test == "queued_idle_stop_keeps_reason") {
    CHECK(bleturner::detail::startAsync());
    CHECK(!bleturner::detail::stopForIdle());
    CHECK(bleturner::status().idleStopped);
    scheduler::run();
    CHECK(host.beginCalls == 0);
    CHECK(bleturner::beforeScreenChange());
    CHECK(bleturner::status().idleStopped);
    CHECK(power_test::locks == 0);
  } else if (test == "suspend_during_sdk_begin") {
    host.beginHook = [&] {
      CHECK(!bleturner::beforeScreenChange());
      CHECK(host.endCalls == 0);
      CHECK(power_test::locks > 0);
    };
    CHECK(bleturner::detail::startAsync());
    scheduler::run();
    CHECK(host.beginHadPower);
    CHECK(!host.endDuringBegin);
    CHECK(bleturner::beforeScreenChange());
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
    CHECK(bleturner::detail::startAsync());
    scheduler::run();
    CHECK(!host.endDuringBegin);
    CHECK(bleturner::beforeScreenChange());
    CHECK(!host.running);
    CHECK(power_test::locks == 0);
    filetransfer::release();
    CHECK(!filetransfer::isActive());
  } else if (test == "power_through_pending_teardown") {
    CHECK(bleturner::detail::startAsync());
    scheduler::run();
    CHECK(host.beginHadPower);
    CHECK(power_test::locks > 0);
    host.endResult = false;
    CHECK(!bleturner::beforeScreenChange());
    CHECK(host.endHadPower);
    CHECK(power_test::locks > 0);
    host.endResult = true;
    CHECK(bleturner::beforeScreenChange());
    CHECK(power_test::locks == 0);
  } else if (test == "sync_begin_power") {
    CHECK(bleturner::switchOn());
    CHECK(host.beginHadPower);
    CHECK(power_test::locks > 0);
    CHECK(bleturner::beforeScreenChange());
    CHECK(host.endHadPower);
    CHECK(power_test::locks == 0);
  } else if (test == "double_start_owns_one_worker") {
    CHECK(bleturner::detail::startAsync());
    CHECK(!bleturner::detail::startAsync());
    CHECK(!bleturner::switchOn());
    CHECK(scheduler::creates == 1);
    CHECK(host.beginCalls == 0);
    scheduler::run();
    CHECK(host.beginCalls == 1);
    CHECK(bleturner::beforeScreenChange());
    CHECK(power_test::locks == 0);
  } else if (test == "main_power_owns_queued_init_and_teardown") {
    power_test::saving = true;
    CHECK(bleturner::detail::startAsync());
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
    CHECK(!bleturner::beforeScreenChange());
    runMainPowerTail();
    CHECK(!power_test::saving);
    CHECK(power_test::enableRequests == 0);
    host.endResult = true;
    CHECK(bleturner::beforeScreenChange());
    runMainPowerTail();
    CHECK(power_test::saving);
    CHECK(power_test::enableRequests == 1);
  } else if (test == "worker_creation_failure_releases_owner") {
    scheduler::createSucceeds = false;
    CHECK(!bleturner::detail::startAsync());
    CHECK(power_test::locks == 0);
    CHECK(bleturner::beforeScreenChange());
    scheduler::createSucceeds = true;
    CHECK(bleturner::detail::startAsync());
    scheduler::run();
    CHECK(host.beginCalls == 1);
    CHECK(bleturner::beforeScreenChange());
  } else if (test == "sdk_begin_failure_releases_power") {
    host.beginResult = false;
    CHECK(bleturner::detail::startAsync());
    scheduler::run();
    CHECK(host.beginHadPower);
    CHECK(bleturner::beforeScreenChange());
    CHECK(power_test::locks == 0);
  } else if (test == "postinit_headroom_pending_teardown") {
    host.changeHeapOnBegin = true;
    host.heapAfterBegin = {40000, 249216, 10000, 32767};
    host.endResult = false;
    CHECK(bleturner::detail::startAsync());
    scheduler::run();
    CHECK(host.stopping);
    CHECK(host.beginHadPower);
    CHECK(host.endHadPower);
    CHECK(power_test::locks > 0);
    CHECK(!bleturner::detail::startAsync());
    CHECK(!bleturner::beforeScreenChange());
    host.endResult = true;
    CHECK(bleturner::beforeScreenChange());
    CHECK(power_test::locks == 0);
  } else return 2;
  std::printf("%s %s (%d failures)\n", failures ? "RED" : "GREEN", argv[1], failures);
  return failures ? 1 : 0;
}
