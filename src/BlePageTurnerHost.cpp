#include "BlePageTurnerHost.h"

#include "HeapMapProbe.h"

#include <GfxRenderer.h>
#include <BoardConfig.h>

#include <optional>

#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST

#include <FontCacheManager.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <BlePageTurner.h>
#include "../lib/BlePageTurner/src/RadioPort.h"
#include "KOReaderPullResume.h"
#include "platform/ColdLogProbe.h"

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

bool preparePageTurnerAfterPull(const bool enabled, const bool usedNetwork, const bool succeeded) {
  // The pre-page restart is an X3 heap repair. X4 Pro uses SDMMC on this path and
  // has no measured proof that the storage handoff is safe.
#if !FREEINK_DEVICE_X3
  return false;
#else
  if (!enabled || !usedNetwork || !succeeded || BoardConfig::isX4Pro()) return false;
  const auto heap = bleHeap();
  const auto& memo = bleturner::port::restartMemo();
  const bool spent = memo.magic == bleturner::kRestartSpentMagic;
  const bool restart = kosyncauto::restartAfterPull(enabled, usedNetwork, succeeded, spent,
                                                   heap.freeBytes, heap.largestBlock);
#ifdef TENOR_PRESS_PROBE
  const auto& need = bleturner::kX3Profile;
  LOG_INF("KAS", "radio restart=%d free=%u largest=%u need_free=%u need_largest=%u", restart,
          static_cast<unsigned>(heap.freeBytes), static_cast<unsigned>(heap.largestBlock),
          static_cast<unsigned>(need.startFreeBytes + kosyncauto::kPullReaderReserveBytes),
          static_cast<unsigned>(need.minimumLargest + kosyncauto::kPullReaderReserveBytes));
  if (restart) restartlog::mark(restartlog::Event::PullHeapRestart, heap.freeBytes, heap.largestBlock);
#endif
  return restart;
#endif
}
void markPageTurnerRestartSpent() { bleturner::port::restartMemo().magic = bleturner::kRestartSpentMagic; }
#else
bool preparePageTurnerAfterPull(bool, bool, bool) { return false; }
void markPageTurnerRestartSpent() {}
#endif
