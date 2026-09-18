#include "BlePageTurnerRuntime.h"

#include <GfxRenderer.h>

#include <atomic>

#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST

#include <FontCacheManager.h>
#include <HalMemory.h>
#include <Logging.h>
#include <BleKeyboardHost.h>

#include <activities/RenderLock.h>

#include "FileTransferState.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define BLE_BEGIN_TASK 1
#endif

namespace freeink::ble {
bool suspendForTransition() {
  auto& host = BleKeyboardHost::getInstance();
  if (!host.isRunning() && !host.isStopping()) return true;
  return host.end(0);
}

namespace {

constexpr size_t kMinimumFreeBytes = 65536;
constexpr size_t kMinimumLargestBlockBytes = 32768;

// One attempt at a time, across tasks. Written by the starter, read while the
// worker finishes. The worker clears it before deleting itself.
std::atomic<bool> attemptInFlight{false};
std::atomic<GfxRenderer*> attemptRenderer{nullptr};

void logSkipped(const char* reason, const HalMemory::HeapStats& heap) {
  LOG_ERR("BLE", "HID begin skipped (%s): free=%zu largest=%zu required_free=%zu required_largest=%zu", reason,
          heap.freeBytes, heap.largestBlockBytes, kMinimumFreeBytes, kMinimumLargestBlockBytes);
}

}  // namespace

bool begin(GfxRenderer& renderer) {
  auto& host = BleKeyboardHost::getInstance();
  if (host.isStopping()) return false;
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
    return false;
  }

  // This function performs one attempt. Callers decide when a later explicit
  // user action is allowed to retry; there is no retry loop here.
  if (!host.begin("FreeInk")) return false;

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
  return true;
}

namespace {

#ifdef BLE_BEGIN_TASK
void attemptTask(void* param) {
  GfxRenderer* renderer = attemptRenderer.load(std::memory_order_acquire);
  if (renderer != nullptr) {
    begin(*renderer);
  }
  attemptInFlight.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}
#endif

}  // namespace

bool beginAsync(GfxRenderer& renderer) {
  auto& host = BleKeyboardHost::getInstance();
  if (host.isRunning()) return true;
#ifdef BLE_BEGIN_TASK
  if (attemptInFlight.load(std::memory_order_acquire)) return false;
  if (host.isStopping()) return false;
  attemptRenderer.store(&renderer, std::memory_order_release);
  attemptInFlight.store(true, std::memory_order_release);
  const BaseType_t ok = xTaskCreate(attemptTask, "ble-start", 3072, nullptr, 2, nullptr);
  if (ok != pdPASS) {
    attemptInFlight.store(false, std::memory_order_release);
    return false;
  }
  return true;
#else
  // Host tests exercise the synchronous path directly.
  return begin(renderer);
#endif
}

}  // namespace freeink::ble

#else

bool freeink::ble::suspendForTransition() { return true; }

bool freeink::ble::begin(GfxRenderer& renderer) {
  (void)renderer;
  return false;
}

bool freeink::ble::beginAsync(GfxRenderer& renderer) {
  (void)renderer;
  return false;
}
#endif

namespace {
std::atomic<bool> readerStartWasDeferred{false};
}  // namespace

bool freeink::ble::readerStartDeferred() { return readerStartWasDeferred.load(std::memory_order_relaxed); }
void freeink::ble::setReaderStartDeferred(const bool deferred) {
  readerStartWasDeferred.store(deferred, std::memory_order_relaxed);
}
