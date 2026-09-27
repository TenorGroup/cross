#include "BlePageTurnerRuntime.h"

#include "BleHeapRestart.h"
#include "HeapMapProbe.h"

#include <GfxRenderer.h>

#include <atomic>
#include <optional>

#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST

#include <FontCacheManager.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <BleKeyboardHost.h>

#include <activities/RenderLock.h>

#include "FileTransferState.h"

#ifdef ARDUINO
#include <esp_attr.h>
#else
#define RTC_NOINIT_ATTR
#endif

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define BLE_BEGIN_TASK 1
#endif

namespace freeink::ble {
namespace {

using bleheap::kMinimumFreeBytes;
using bleheap::kMinimumLargestBlockBytes;

// One attempt at a time, across tasks. Written by the starter, read while the
// worker finishes. The worker clears it before deleting itself.
std::atomic<bool> attemptInFlight{false};
std::atomic<GfxRenderer*> attemptRenderer{nullptr};
std::atomic<bool> attemptCancelled{false};
// The starter owns this until the worker finishes; teardown then runs on main.
// Keeping the lock through the running state also closes gaps between polls.
std::optional<HalPowerManager::Lock> radioPowerLock;
#ifdef TENOR_UI_ACCEPTANCE
std::atomic<uint32_t> startStackMinimum{0};
#endif
// Outlives ESP.restart: the one heap restart already spent since the radio last came up.
RTC_NOINIT_ATTR bleheap::Memo heapRestartMemo;
// Written by the start (one at a time), read by main once no start is in flight.
bleheap::Tracker heapTracker{heapRestartMemo};

void logSkipped(const char* reason, const HalMemory::HeapStats& heap) {
  LOG_ERR("BLE", "HID begin skipped (%s): free=%zu largest=%zu required_free=%zu required_largest=%zu", reason,
          heap.freeBytes, heap.largestBlockBytes, kMinimumFreeBytes, kMinimumLargestBlockBytes);
  static uint8_t mapsLeft = 2;
  if (mapsLeft > 0) {
    mapsLeft--;
    heapMapDump("ble-skipped");
  }
}

bool beginOwned(GfxRenderer& renderer) {
  auto& host = BleKeyboardHost::getInstance();
  if (attemptCancelled.load(std::memory_order_acquire) || host.isStopping()) return false;
  if (host.isRunning()) return true;

  const auto before = HalMemory::getInternalHeap();
  if (filetransfer::isActive()) {
    logSkipped("filetransfer-active", before);
    return false;
  }

  // Font caches are rebuildable and can occupy a large fragmented block. The
  // cache manager must only be touched while the renderer is quiescent.
  // Keep rendering quiescent through the heap check and allocation. Otherwise
  // the render task can refill the released cache between preflight and init.
  // Hold the lock ONLY for the bounded release + preflight below: the stack
  // start itself runs outside the render lock so a paint in flight is never
  // starved by radio init (a main-task stall here froze the whole UI).
  {
    RenderLock lock;
    if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
  }

  const auto heap = HalMemory::getInternalHeap();
  if (heap.freeBytes < kMinimumFreeBytes || heap.largestBlockBytes < kMinimumLargestBlockBytes) {
    logSkipped("insufficient-internal-heap", heap);
    heapTracker.refused(heap.freeBytes, heap.largestBlockBytes);
    return false;
  }
  heapTracker.passed();

#ifdef TENOR_PRESS_PROBE
  // What the stack comes up in: the reader released its layout parser before this (readyForRadio).
  LOG_INF("BLE", "HID begin heap free=%zu largest=%zu", heap.freeBytes, heap.largestBlockBytes);
#endif
  // This function performs one attempt. Callers decide when a later explicit
  // user action is allowed to retry; there is no retry loop here.
  if (attemptCancelled.load(std::memory_order_acquire) || filetransfer::isActive()) return false;
  if (!host.begin("FreeInk")) return false;
  if (attemptCancelled.load(std::memory_order_acquire) || filetransfer::isActive()) {
    host.end(0);
    return false;
  }

  // BLE may initialize successfully while consuming the reader's last large
  // block. InflateReader::RING_BYTES and the streaming miniz window both need
  // 32768 contiguous bytes. Keep that minimum available after stack allocation.
  const auto after = HalMemory::getInternalHeap();
  if (after.largestBlockBytes < kMinimumLargestBlockBytes) {
    LOG_ERR("BLE", "HID begin rolled back (post-init-headroom): free=%zu largest=%zu required_largest=%zu",
            after.freeBytes, after.largestBlockBytes, kMinimumLargestBlockBytes);
    // Teardown can wait for the BLE worker. The caller's context is already
    // outside RenderLock, so a bounded end keeps the main loop responsive.
    host.end(0);
    return false;
  }
#ifdef TENOR_PRESS_PROBE
  LOG_INF("BLE", "HID begin kept free=%zu largest=%zu", after.freeBytes, after.largestBlockBytes);
#endif
  heapTracker.radioUp();
  return true;
}

void finishAttempt(const bool started, const bool reportReaderFailure) {
  auto& host = BleKeyboardHost::getInstance();
  if (reportReaderFailure) {
    setReaderStartDeferred(!started && !attemptCancelled.load(std::memory_order_acquire));
  }
  if (!host.isRunning() && !host.isStopping()) radioPowerLock.reset();
  attemptRenderer.store(nullptr, std::memory_order_release);
  attemptInFlight.store(false, std::memory_order_release);
}

#ifdef BLE_BEGIN_TASK
void attemptTask(void* param) {
  GfxRenderer* renderer = attemptRenderer.load(std::memory_order_acquire);
  const bool started = renderer != nullptr && beginOwned(*renderer);
#ifdef TENOR_UI_ACCEPTANCE
  startStackMinimum.store(uxTaskGetStackHighWaterMark(nullptr), std::memory_order_release);
#endif
  finishAttempt(started, true);
  vTaskDelete(nullptr);
}
#endif

}  // namespace

#ifdef TENOR_UI_ACCEPTANCE
uint32_t startStackHighWaterMark() { return startStackMinimum.load(std::memory_order_acquire); }
#endif

bool initializing() { return attemptInFlight.load(std::memory_order_acquire); }

bool heapRestartWanted() { return heapTracker.wanted.load(std::memory_order_acquire); }

void markHeapRestart() {
  heapTracker.restarting();
}

bool busy() {
  if (attemptInFlight.load(std::memory_order_acquire)) return true;
  auto& host = BleKeyboardHost::getInstance();
  return host.isRunning() || host.isStopping();
}

bool suspendForTransition(const uint32_t timeoutMs) {
  // Never call SDK end concurrently with NimBLE init. The worker still owns
  // its allocations until finishAttempt publishes completion.
  attemptCancelled.store(true, std::memory_order_release);
  if (attemptInFlight.load(std::memory_order_acquire)) return false;
  auto& host = BleKeyboardHost::getInstance();
  if (host.isRunning() || host.isStopping()) {
    if (!radioPowerLock) radioPowerLock.emplace();
    if (!host.end(timeoutMs)) return false;
  }
  radioPowerLock.reset();
  return true;
}

bool begin(GfxRenderer& renderer) {
  setIdleStopped(false);
  setReaderStartDeferred(false);
  bool expected = false;
  if (!attemptInFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
  attemptCancelled.store(false, std::memory_order_release);
  if (!radioPowerLock) radioPowerLock.emplace();
  const bool started = beginOwned(renderer);
  finishAttempt(started, false);
  return started;
}

bool beginAsync(GfxRenderer& renderer) {
#ifdef BLE_BEGIN_TASK
  setIdleStopped(false);
  setReaderStartDeferred(false);
  bool expected = false;
  if (!attemptInFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
  auto& host = BleKeyboardHost::getInstance();
  if (host.isStopping() || host.isRunning()) {
    const bool running = host.isRunning();
    attemptInFlight.store(false, std::memory_order_release);
    return running;
  }
  attemptCancelled.store(false, std::memory_order_release);
  if (!radioPowerLock) radioPowerLock.emplace();
  attemptRenderer.store(&renderer, std::memory_order_release);
  const BaseType_t ok = xTaskCreate(attemptTask, "ble-start", 3072, nullptr, 2, nullptr);
  if (ok != pdPASS) {
    finishAttempt(false, true);
    return false;
  }
  return true;
#else
  return begin(renderer);
#endif
}

bool stopForIdle() {
  setIdleStopped(true);
  setReaderStartDeferred(false);
  return suspendForTransition();
}

}  // namespace freeink::ble

#else

bool freeink::ble::suspendForTransition(uint32_t) { return true; }
bool freeink::ble::busy() { return false; }
bool freeink::ble::initializing() { return false; }
bool freeink::ble::heapRestartWanted() { return false; }
void freeink::ble::markHeapRestart() {}
#ifdef TENOR_UI_ACCEPTANCE
uint32_t freeink::ble::startStackHighWaterMark() { return 0; }
#endif
bool freeink::ble::stopForIdle() { return true; }

bool freeink::ble::begin(GfxRenderer& renderer) {
  (void)renderer;
  setIdleStopped(false);
  setReaderStartDeferred(false);
  return false;
}

bool freeink::ble::beginAsync(GfxRenderer& renderer) {
  (void)renderer;
  setIdleStopped(false);
  setReaderStartDeferred(false);
  return false;
}
#endif

namespace {
std::atomic<bool> readerStartWasDeferred{false};
std::atomic<bool> radioWasIdleStopped{false};
std::atomic<bool> rearmRequested{false};
std::atomic<bool> heldForBuild{false};
}  // namespace

void freeink::ble::requestRearm() {
  heldForBuild.store(false, std::memory_order_relaxed);
  rearmRequested.store(true, std::memory_order_relaxed);
}
bool freeink::ble::radioHeldForBuild() { return heldForBuild.load(std::memory_order_relaxed); }
void freeink::ble::setRadioHeldForBuild(const bool held) { heldForBuild.store(held, std::memory_order_relaxed); }
bool freeink::ble::takeRearmRequest() { return rearmRequested.exchange(false, std::memory_order_relaxed); }

bool freeink::ble::readerStartDeferred() { return readerStartWasDeferred.load(std::memory_order_relaxed); }
void freeink::ble::setReaderStartDeferred(const bool deferred) {
  readerStartWasDeferred.store(deferred, std::memory_order_relaxed);
}

bool freeink::ble::idleStopped() { return radioWasIdleStopped.load(std::memory_order_relaxed); }
void freeink::ble::setIdleStopped(const bool stopped) {
  radioWasIdleStopped.store(stopped, std::memory_order_relaxed);
}
