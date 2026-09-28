#include "BlePageTurnerHost.h"

#include "HeapMapProbe.h"

#include <GfxRenderer.h>

#include <optional>

#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST

#include <FontCacheManager.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <BlePageTurner.h>

#include "FileTransferState.h"
#include "activities/Activity.h"

namespace {

GfxRenderer* bleRenderer = nullptr;
void (*runShortcut)(bleturner::Action) = nullptr;
void (*restartBook)() = nullptr;

bleturner::Heap bleHeap() {
  const auto heap = HalMemory::getInternalHeap();
  return {heap.freeBytes, heap.largestBlockBytes};
}

// Font caches are rebuildable and can occupy a large fragmented block. The cache manager is
// only touched while the renderer is quiescent, and the lock is only TRIED: the render task
// can hold it through a whole chapter build that is waiting for this very radio.
bool bleReleaseCaches() {
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) return false;
  if (auto* fcm = bleRenderer->getFontCacheManager()) fcm->releaseSdFontCaches();
  return true;
}

bool bleDeliver(const bleturner::Action a) {
  switch (a) {
    case bleturner::Action::NextPage:
      return activityManager.pageTurn(true);
    case bleturner::Action::PrevPage:
      return activityManager.pageTurn(false);
    case bleturner::Action::NextChapter:
      return activityManager.chapterSkip(true);
    case bleturner::Action::PrevChapter:
      return activityManager.chapterSkip(false);
    // Catalog actions: the remote asks for them like every other trigger does.
    case bleturner::Action::ReaderMenu:
    case bleturner::Action::SaveQuote:
      runShortcut(a);
      return true;
    default:
      return false;
  }
}

bool bleYieldForRadio() { return activityManager.yieldForegroundReaderForRadio(); }
bool bleFileTransfer() { return filetransfer::isActive(); }
void bleRestartIntoBook() { restartBook(); }

// The BLE controller needs a steady clock: the radio holds the CPU at full speed while it is
// up or changing state.
std::optional<HalPowerManager::Lock> bleFullSpeed;
void bleHoldFullSpeed(const bool hold) {
  if (!hold) {
    bleFullSpeed.reset();
  } else if (!bleFullSpeed) {
    bleFullSpeed.emplace();
  }
}

void bleLog(const bool error, const char* format, va_list args) {
#ifdef ENABLE_SERIAL_LOG
  if (error || LOG_LEVEL >= 1) vlogPrintf(error ? "ERR" : "INF", "BLE", format, args);
#else
  (void)error;
  (void)format;
  (void)args;
#endif
}

#if defined(TENOR_PRESS_PROBE) && !defined(SIMULATOR)
void bleHeapMap(const char* tag) { heapMapDump(tag); }
#else
constexpr void (*bleHeapMap)(const char*) = nullptr;
#endif

const bleturner::Host bleHost{bleHeap,         bleReleaseCaches,   bleDeliver,
                              bleYieldForRadio, bleFileTransfer,    bleRestartIntoBook,
                              bleHoldFullSpeed, bleLog,             bleHeapMap};

}  // namespace

void beginPageTurner(GfxRenderer& renderer, bleturner::Config& config, void (*shortcut)(bleturner::Action),
                     void (*restartIntoBook)()) {
  bleRenderer = &renderer;
  runShortcut = shortcut;
  restartBook = restartIntoBook;
  bleturner::begin(bleHost, config);
}

#endif
