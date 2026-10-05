#include "ActivityManager.h"

#include <BlePageTurner.h>
#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalMemory.h>
#include <HalFrontlight.h>
#include <HalPowerManager.h>
#include <Memory.h>
#include <VectorFontSupport.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "OpdsServerStore.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "components/HeaderBackTapTarget.h"
#include "components/TenorMenuChrome.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/HomeActivity.h"
#include "shells/Shell.h"
#include "library/LibraryListActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "network/UsbDriveActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/BmpViewerActivity.h"
#include "util/FrontlightPanelActivity.h"
#include "util/FullScreenMessageActivity.h"

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;

#ifdef TENOR_UI_ACCEPTANCE
uint32_t ActivityManager::renderStackHighWaterMark() const {
#ifndef SIMULATOR
  return renderTaskHandle ? uxTaskGetStackHighWaterMark(renderTaskHandle) : 0;
#else
  return 0;
#endif
}
#endif

namespace {
// Writes a closing screen left for later (deferWrite). Set and run on the main task only; the
// render task says when a frame has been drawn since.
void (*deferredWrites[4])() = {};
std::atomic<bool> frameAfterDeferredWrite{false};
std::atomic<bool> frameDrawn{false};
}  // namespace

// The Home key leads to the active shell's main screen: Diary for Ugly, Recent for touch Cross.
HomeMenuItem ActivityManager::homeKeyTarget() const {
  return tenorchrome::kTouchShell && !shell::isUgly() ? HomeMenuItem::RECENTS : HomeMenuItem::NONE;
}

const char* ActivityManager::currentName() const { return currentActivity ? currentActivity->name.c_str() : nullptr; }

namespace {
// Touch shell: the frame about to reach the panel gets the dynamic bar the screen in front declares.
void drawFootBar(const GfxRenderer& r) {
  tenorchrome::drawFootBar(r, tenorchrome::footBarFor(activityManager.currentName()), activityManager.footZone());
  activityManager.drawLightGesture(r);
}
void saveGestureLight() { SETTINGS.saveToFile(); }
void drawGrayLightGesture(const GfxRenderer& r) { activityManager.drawLightGesture(r); }
}  // namespace

void ActivityManager::drawLightGesture(const GfxRenderer& r) const {
  lightGesture.draw(r, tenorchrome::touchBarTop(r.getScreenHeight()));
}

void ActivityManager::deferLightGestureSave() {
  if (!lightGesture.dirty) return;
  lightGesture.dirty = false;
  deferWrite(&saveGestureLight);
}

// The level the two-finger gesture shows, onto the light: brightness (0 = off, the brightness kept for the
// next step up) or warmth.
bool ActivityManager::applyLightLevel() {
  if (!lightGesture.vertical) {
    if (Frontlight.hasColorTemperature()) Frontlight.setWarmth(lightGesture.value);
  } else if (lightGesture.value) {
    Frontlight.setBrightness(lightGesture.value);
    Frontlight.setOn(true);
  } else {
    Frontlight.setOn(false);
  }
  return true;
}

// Two fingers set the light while they move (FrontlightGesture). The light changes on this pass; the level
// shows on the next frame the render task draws. True while two fingers are down or their release is being
// consumed, so no screen reads them as a tap or a swipe.
bool ActivityManager::handleLightGesture() {
  if (!BoardConfig::isX4Pro()) return false;
  uint8_t down = 0;
  int x = 0, y = 0;
  const bool two = mappedInput.touchContactsAt(down, x, y) && down == 2;
  uint8_t contacts = 0;
  int dx = 0, dy = 0;
  unsigned long ms = 0;
  const bool released = mappedInput.popMultiTouchSwipe(contacts, dx, dy, &ms);
  if (!two && !released && !lightGesture.following()) return false;
  if (!two) lightGesture.lift();
  if (!Frontlight.present()) return true;
  // ponytail: the render task reads these few bytes without the render lock (a torn read draws one stale
  // number for one frame); taking the lock here would hold the light until the panel finished a refresh.
  const uint32_t now = millis();
  bool changed = false;
  if (two && lightGesture.follow(x, y, Frontlight.brightness(), Frontlight.warmth(), Frontlight.isOn(), now))
    changed = applyLightLevel();
  if (released && contacts == 2) {
    if (lightGesture.flick(dx, dy, ms, Frontlight.brightness(), now)) {
      changed = true;
      Frontlight.setBrightness(lightGesture.keep);
      Frontlight.setOn(false);
    } else if (lightGesture.settle(dx, dy, ms, Frontlight.brightness(), Frontlight.warmth(), Frontlight.isOn(), now)) {
      changed = applyLightLevel();
    }
  }
#ifdef TENOR_PRESS_PROBE
  if (released) LOG_INF("LGT", "release contacts=%u dx=%d dy=%d ms=%lu", contacts, dx, dy, ms);
#endif
  if (!changed) return true;
  lightGesture.dirty = lightGesture.dirty || SETTINGS.frontlightBrightness != Frontlight.brightness() ||
                       SETTINGS.frontlightWarmth != Frontlight.warmth() ||
                       SETTINGS.frontlightOn != (Frontlight.isOn() ? 1 : 0);
  SETTINGS.frontlightBrightness = Frontlight.brightness();
  SETTINGS.frontlightWarmth = Frontlight.warmth();
  SETTINGS.frontlightOn = Frontlight.isOn() ? 1 : 0;
  requestUpdate();
  return true;
}

// The top menu: the light panel, over whatever screen asked for it (not over itself).
void ActivityManager::openTopMenu() {
  if (currentActivity && currentActivity->name == "FrontlightPanel") return;
  pushActivity(std::make_unique<FrontlightPanelActivity>(renderer, mappedInput));
}

tenorchrome::Zone ActivityManager::footZone() const {
  if (isReaderActivity()) return tenorchrome::Zone::Book;
  switch (homeMenuOrigin()) {
    case HomeMenuItem::FILE_BROWSER:
      return tenorchrome::Zone::File;
    case HomeMenuItem::STATS_TAB:
      return tenorchrome::Zone::Stats;
    case HomeMenuItem::SETTINGS_MENU:
      return tenorchrome::Zone::Settings;
    case HomeMenuItem::FAVORITES_TAB:
      return tenorchrome::Zone::Favorites;
    default:
      return tenorchrome::Zone::Recent;
  }
}

// The zone's icon in the bar: back to the book's page, or to the Home card the screen was reached from.
// Each screen above the book closes as its Back would (a cancelled result), one per pass.
void ActivityManager::goZoneRoot() {
  if (!isReaderActivity()) {
    goHome(homeMenuOrigin());
    return;
  }
  if (currentActivity->isReaderActivity()) return;
  toReaderPage = true;
  currentActivity->result.isCancelled = true;
  popActivity();
}

void ActivityManager::begin() {
  if (tenorchrome::kTouchShell) {
    GfxRenderer::preDisplayHook = &drawFootBar;
    GfxRenderer::preGrayUploadHook = &drawGrayLightGesture;
  }
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
#if CROSSPOINT_VECTOR_FONTS
  // FreeType rasterization runs on this task, and the deepest observed chain
  // is a glyph fault DURING LAYOUT: expat + parser + line-layout frames
  // (~3.5KB on Xtensa) with the scan converter's FT_RENDER_POOL_SIZE (4KB)
  // stack-resident band pool on top - a measured ~8KB peak that trips the
  // canary on an 8KB stack. Vector-font boards all have PSRAM-class RAM.
  constexpr uint32_t renderTaskStackBytes = 16384;
#else
  constexpr uint32_t renderTaskStackBytes = 8192;
#endif
  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                          renderTaskStackBytes,  // Stack size
                          this,                  // Parameters
                          1,                     // Priority
                          &renderTaskHandle,     // Task handle
                          renderTaskCore  // Keep long renders/cover decodes off CPU 0's idle watchdog when available
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    RenderLock lock;
    if (currentActivity && !sleepTransition) {
      // A refresh still running from before (the wake notice) reads the framebuffer to the end:
      // nothing draws over it until it is done. Without a notice there is nothing to wait for.
      if (SleepActivity::takeWakeNoticeRunning()) renderer.waitRefreshComplete();
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      // Night mode is a global output polarity applied to every activity.
      // The sleep screen forces normal polarity itself (SleepActivity).
      display.setInverted(SETTINGS.screenInverted != 0);
      currentActivity->render(std::move(lock));
      frameAfterDeferredWrite.store(true, std::memory_order_release);
      frameDrawn.store(true, std::memory_order_release);
    }
    // Notify any task blocked in requestUpdateAndWait() that the render is done.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&activityManagerSpinlock);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&activityManagerSpinlock);
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

void ActivityManager::loop() {
  if (mappedInput.consumeSuppressedRelease()) return;

  if (currentActivity && currentActivity->requiresExclusiveStorageLoop()) {
    // Drain a classified multi-contact release while USB owns storage. It must
    // never become a single-contact action or a deferred settings write.
    uint8_t contacts = 0;
    int dx = 0, dy = 0;
    mappedInput.popMultiTouchSwipe(contacts, dx, dy);
    currentActivity->loop();
    // An exclusive-storage activity must restart rather than navigate away:
    // processing a pending action here could re-enable filesystem users while
    // the USB host still owns the raw SD card.
    if (requestedUpdate.exchange(false) && renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
    return;
  }

  if (!sleepTransition && pendingAction == PendingAction::None && handleLightGesture()) return;
  if (lightGesture.expired(millis())) {
    RenderLock lock;
    lightGesture.visible = false;
    deferLightGestureSave();
    requestUpdate();
  }

  // The next screen's first frame is up: now the writes the screen before it left (deferWrite),
  // under the render lock so they never run beside a paint.
  if (deferredWrites[0] && frameAfterDeferredWrite.load(std::memory_order_acquire)) {
    RenderLock lock(RenderLock::TryTake{});
    if (lock.acquired()) flushDeferredWrites();
  }

  if (currentActivity && !sleepTransition && pendingAction == PendingAction::None) {
    const bool heldBack = mappedInput.wasLongPressed(MappedInputManager::Button::Back, 1000);
    const bool bottomHome = mappedInput.wasBottomHomeGesture();
    if (!currentActivity->isHomeActivity() && (heldBack || mappedInput.wasHomeGesture())) {
      const HomeMenuItem homeTarget = bottomHome ? HomeMenuItem::RECENTS : homeKeyTarget();
      if (currentActivity->saveInputBeforeHome()) {
        homeAfterInput = true;
        homeAfterInputTarget = homeTarget;
        return;
      }
      if (!bottomHome && currentActivity->handleHomeGesture()) {
        if (heldBack) {
          homeAfterInput = true;
          homeAfterInputTarget = homeTarget;
        }
        return;
      }
      goHome(homeTarget);
      return;
    }
    // Touch: the Home key from the Home screen itself brings it back to its default card, Recent.
    if (tenorchrome::kTouchShell && currentActivity->isHomeActivity() && mappedInput.wasHomeGesture() &&
        (bottomHome || homeMenuOrigin() != HomeMenuItem::RECENTS)) {
      goHome(HomeMenuItem::RECENTS);
      return;
    }

    // Touch: the "B" of a remote that links blinks in the status strip (not over a book's page).
    if (tenorchrome::kTouchShell && !currentActivity->isReaderActivity() && tenorchrome::bluetoothBlinkDue())
      requestUpdate();

    // Touch: the zone's icon in the dynamic bar.
    if (tenorchrome::kTouchShell) {
      int tx = 0;
      int ty = 0;
      if (mappedInput.wasScreenTapped(tx, ty) && HeaderBackTapTarget::zoneContains(tx, ty)) {
        goZoneRoot();
        return;
      }
    }

    // Tap-first control-center entry: a tap on the status band opens the top menu (the light panel),
    // mirroring the top-edge swipe (which some panels' etched glass makes unreliable). Touch shell: on
    // every screen that draws the strip, the strip alone, so a tap on the first row of a list is the
    // row's; going back is the bar's "<". Other touch boards: the band of the top-level tab screens.
    bool statusBarTap = false;
    if (mappedInput.hasTouch() &&
        (tenorchrome::kTouchShell ? HeaderBackTapTarget::strip
                                  : currentActivity->name == "Home" || currentActivity->name == "FileBrowser" ||
                                        currentActivity->name == "Settings" ||
                                        currentActivity->name == "NetworkModeSelection")) {
      int tx = 0;
      int ty = 0;
      // The header back button shares this band; its taps stay Back.
      const int band = tenorchrome::kTouchShell ? tenorchrome::TOUCH_STRIP_HEIGHT : 44;
      statusBarTap = mappedInput.wasScreenTapped(tx, ty) && ty < band && !HeaderBackTapTarget::contains(tx, ty);
    }
    // Both ways in are touch gestures, so a build without a touch board leaves the panel out.
    if (BoardConfig::hasTouch() && currentActivity->name != "FrontlightPanel" &&
        (statusBarTap || mappedInput.wasLightPanelGesture())) {
      openTopMenu();
      return;
    }

    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    currentActivity->onTick();
    currentActivity->loop();
  }

  while (pendingAction != PendingAction::None) {
    // Release the radio before the next activity allocates its fonts, cover or
    // inflate buffers. Teardown can span loop iterations and must run outside
    // RenderLock so the worker and renderer can finish without deadlocking.
#ifdef TENOR_PRESS_PROBE
    static uint32_t radioWaitFrom = 0;
    if (!radioWaitFrom) radioWaitFrom = millis() | 1;
    if (!bleturner::beforeScreenChange()) return;
    LOG_INF("ACT", "Transition radio=%lu ms", static_cast<unsigned long>(millis() - radioWaitFrom));
    radioWaitFrom = 0;
#else
    if (!bleturner::beforeScreenChange()) return;
#endif
    ++activityGeneration_;

    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        const HomeMenuItem homeTarget = homeAfterInput ? homeAfterInputTarget : HomeMenuItem::NONE;
        homeAfterInput = false;
        homeAfterInputTarget = HomeMenuItem::NONE;
        lock.unlock();  // goHome may acquire its own lock
        goHome(homeTarget);
        continue;  // Will launch goHome immediately

      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        currentActivity->onResume();
        // Handle result if necessary
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");

          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          handler(pendingResult);
        }

        if (homeAfterInput) {
          const HomeMenuItem homeTarget = homeAfterInputTarget;
          homeAfterInput = false;
          homeAfterInputTarget = HomeMenuItem::NONE;
          lock.unlock();
          goHome(homeTarget);
          continue;
        }
        // On the way down to the book's page (the zone icon): the next screen closes too.
        if (toReaderPage) {
          if (currentActivity->isReaderActivity()) {
            toReaderPage = false;
          } else if (pendingAction == PendingAction::None) {
            currentActivity->result.isCancelled = true;
            popActivity();
            continue;
          }
        }
        if (pendingAction == PendingAction::None) {
          lock.unlock();
          currentActivity->openPendingSettingsSibling();
        }

        // Request an update to ensure the popped activity gets re-rendered
        if (pendingAction == PendingAction::None) {
          requestUpdate();
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity) {
      // Current activity has requested a new activity to be launched
      RenderLock lock;

      if (pendingAction == PendingAction::Replace) {
        homeAfterInput = false;
        homeAfterInputTarget = HomeMenuItem::NONE;
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        // Move current activity to stack
        if (currentActivity) {
          saveNavigation(*currentActivity);
          currentActivity->onPause();
        }
        stackActivities.push_back(std::move(currentActivity));
        // The parent's header back rect must not route taps on the pushed
        // screen (which may draw no header of its own).
        HeaderBackTapTarget::clear();
        HeaderBackTapTarget::clearFoot();
        HeaderBackTapTarget::strip = false;
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      pendingAction = PendingAction::None;
      currentActivity = std::move(pendingActivity);

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();
      {
        const auto heap = HalMemory::getInternalHeap();
        LOG_INF("HEAP", "enter %s free=%u largest=%u", currentActivity->name.c_str(),
                static_cast<unsigned>(heap.freeBytes), static_cast<unsigned>(heap.largestBlockBytes));
      }
      restoreNavigation();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (requestedUpdate.exchange(false)) {
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

HomeMenuItem ActivityManager::homeMenuOrigin() const {
  MenuNavigationState state;
  bool found = navigationMemory.load("Home", state);
  for (const auto& activity : stackActivities) {
    if (activity && activity->isHomeActivity()) {
      activity->captureNavigation(state);
      found = true;
      break;
    }
  }
  if (currentActivity && currentActivity->isHomeActivity()) {
    currentActivity->captureNavigation(state);
    found = true;
  }
  if (!found) return HomeMenuItem::NONE;
  switch (state.tab) {
    case 1:
      return HomeMenuItem::FILE_BROWSER;
    case 2:
      return HomeMenuItem::STATS_TAB;
    case 3:
      return HomeMenuItem::SETTINGS_MENU;
    case 4:
      return HomeMenuItem::FAVORITES_TAB;
    default:
      return HomeMenuItem::RECENTS;
  }
}

void ActivityManager::saveNavigation(Activity& activity) {
  if (!activity.remembersNavigation()) return;
  MenuNavigationState state;
  activity.captureNavigation(state);
  navigationMemory.save(activity.navigationMemoryKey(), state);
}

bool ActivityManager::hasDrawnFrame() const { return frameDrawn.load(std::memory_order_acquire); }

void ActivityManager::deferWrite(void (*write)()) {
  frameAfterDeferredWrite.store(false, std::memory_order_relaxed);
  for (auto& slot : deferredWrites) {
    if (slot == write) return;
    if (!slot) {
      slot = write;
      return;
    }
  }
  write();
}

void ActivityManager::flushDeferredWrites() {
  if (!deferredWrites[0]) return;
  const uint32_t started = millis();
  unsigned count = 0;
  for (auto& slot : deferredWrites) {
    if (!slot) break;
    const auto write = slot;
    slot = nullptr;
#ifdef TENOR_PRESS_PROBE
    // One line per write: a slow card shows here as one file, not as the whole batch.
    const uint32_t writeStarted = millis();
    write();
    LOG_INF("ACT", "Deferred write i=%u ms=%lu", count, static_cast<unsigned long>(millis() - writeStarted));
#else
    write();
#endif
    ++count;
  }
  LOG_INF("ACT", "Deferred writes n=%u ms=%lu", count, static_cast<unsigned long>(millis() - started));
}

void ActivityManager::closeForRestart() {
  RenderLock lock;
  homeAfterInput = false;
  homeAfterInputTarget = HomeMenuItem::NONE;
  exitActivity(lock);
  flushDeferredWrites();
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    lightGesture.visible = false;
    deferLightGestureSave();
    // What the screen before this one left is written before this one leaves anything.
    flushDeferredWrites();
    saveNavigation(*currentActivity);
    const uint32_t started = millis();
    const std::string exited = currentActivity->name;
    currentActivity->onExit();
#ifdef TENOR_PRESS_PROBE
    const uint32_t exitedMs = millis();
#endif
    currentActivity.reset();
    {
      const auto heap = HalMemory::getInternalHeap();
      LOG_INF("HEAP", "exit %s free=%u largest=%u", exited.c_str(), static_cast<unsigned>(heap.freeBytes),
              static_cast<unsigned>(heap.largestBlockBytes));
    }
#ifdef TENOR_PRESS_PROBE
    LOG_INF("ACT", "Exit stages onExit=%lu destroy=%lu", static_cast<unsigned long>(exitedMs - started),
            static_cast<unsigned long>(millis() - exitedMs));
#endif
    if (sleepTransition) LOG_INF("SLP", "Timing close-activity=%lu ms", static_cast<unsigned long>(millis() - started));
  }
}

void ActivityManager::restoreNavigation() {
  if (!currentActivity || !currentActivity->remembersNavigation()) return;
  MenuNavigationState state;
  if (navigationMemory.load(currentActivity->navigationMemoryKey(), state)) {
    currentActivity->restoreNavigation(state);
    LOG_DBG("NAV", "Restored %s tab=%d row=%d", currentActivity->navigationMemoryKey().c_str(), state.tab,
            state.tab >= 0 && state.tab < state.count ? state.cursors[state.tab].selected : 0);
    requestUpdate();
  }
  // The outgoing screen's header back button must not eat taps on the next
  // screen; the next header draw re-records it.
  HeaderBackTapTarget::clear();
  HeaderBackTapTarget::clearFoot();
  HeaderBackTapTarget::strip = false;
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  mappedInput.resetHomeButtonInput();
  if (currentActivity && newActivity && currentActivity->remembersNavigation() && !newActivity->isHomeActivity() &&
      newActivity->name != "Sleep") {
    navigationMemory.enter(currentActivity->navigationMemoryKey(), newActivity->navigationMemoryKey());
  }
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  if (currentActivity) {
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately
    currentActivity = std::move(newActivity);
    currentActivity->onEnter();
    restoreNavigation();
  }
}

void ActivityManager::goToFileTransfer() {
  // Opened from a book, File transfer is a detour: it hands the reader back on
  // exit. The reader has already saved its position, it does so on every paint.
  std::string book = isReaderActivity() ? APP_STATE.openEpubPath : std::string();
  replaceActivity(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput, std::move(book)));
}

void ActivityManager::goToUsbDrive() {
#if FREEINK_CAP_USB_MSC
  auto activity = makeUniqueNoThrow<UsbDriveActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: USB Drive activity");
    return;
  }
  replaceActivity(std::move(activity));
#else
  LOG_ERR("ACT", "USB Drive requested in a build without USB Drive capability");
#endif
}

void ActivityManager::goToSettings(const int theBanDau) {
  replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput, theBanDau));
}

void ActivityManager::goToFileBrowser(std::string path) {
  replaceActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput, std::move(path)));
}

void ActivityManager::goToLibrary() {
  auto activity = makeUniqueNoThrow<LibraryListActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: library activity");
    return;
  }
  replaceActivity(std::move(activity));
}

void ActivityManager::goToBrowser() {
  const auto& servers = OPDS_STORE.getServers();
  // Skip the server picker when there's only one server configured
  if (servers.size() == 1) {
    replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, servers[0]));
  } else {
    replaceActivity(std::make_unique<OpdsServerListActivity>(renderer, mappedInput, true));
  }
}

void ActivityManager::goToReader(std::string path, const bool allowFastInitialRefresh) {
  if (path.empty()) {
    goToFileBrowser("/");
    return;
  }

  if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path)) {
    auto activity = makeUniqueNoThrow<BmpViewerActivity>(renderer, mappedInput, std::move(path));
    if (!activity) {
      LOG_ERR("ACT", "OOM: bitmap viewer activity");
      return;
    }
    replaceActivity(std::move(activity));
    return;
  }

  auto activity = ReaderActivity::create(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  if (activity) {
    replaceActivity(std::move(activity));
  }
}

bool ActivityManager::goToSleep(bool fromTimeout) {
  const uint32_t started = millis();
  {
    RenderLock lock;
    sleepTransition = true;
    requestedUpdate = false;
    SleepActivity::showEnteringSleep(renderer);
    LOG_INF("SLP", "Timing notice-and-lock=%lu ms", static_cast<unsigned long>(millis() - started));
  }
  replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput, fromTimeout));
  // The radio starter/connection worker may need several polls before it gives
  // up ownership. Only onEnter of SleepActivity completes the retained image.
  while (pendingAction != PendingAction::None) {
    loop();
    if (pendingAction == PendingAction::None) break;
    if (millis() - started >= 5000) {
      LOG_ERR("SLP", "Sleep transition timed out; keeping display and storage awake");
      pendingActivity.reset();
      pendingAction = PendingAction::None;
      {
        RenderLock lock;
        sleepTransition = false;
      }
      requestUpdate();
      return false;
    }
    delay(10);
  }
  return currentActivity && currentActivity->name == "Sleep";
}

void ActivityManager::goToBoot() { replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goHome(HomeMenuItem initialMenuItem, bool cleanInitialRefresh) {
  // The saved cursor of the tenor/cross Home means nothing to another shell.
  if (initialMenuItem == HomeMenuItem::NONE && !shell::isUgly()) initialMenuItem = homeMenuOrigin();
  if (initialMenuItem == HomeMenuItem::NONE && currentActivity) {
    const auto& activityName = currentActivity->name;
    if (activityName == "FileBrowser") {
      initialMenuItem = HomeMenuItem::FILE_BROWSER;
    } else if (activityName == "Library") {
      initialMenuItem = HomeMenuItem::LIBRARY;
    } else if (activityName == "OpdsBookBrowser") {
      initialMenuItem = HomeMenuItem::OPDS_BROWSER;
    } else if (activityName == "CrossPointWebServer") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "Settings") {
      initialMenuItem = HomeMenuItem::SETTINGS_MENU;
    }
  }
  replaceActivity(shell::makeHome(renderer, mappedInput, initialMenuItem, cleanInitialRefresh));
}
void ActivityManager::goToCrashReport() { replaceActivity(std::make_unique<CrashActivity>(renderer, mappedInput)); }

bool ActivityManager::switchSettingsSibling(const int direction) {
  if (!currentActivity || stackActivities.empty() || pendingAction != PendingAction::None) return false;
  if (!stackActivities.back()->selectSettingsSibling(direction)) return false;
  popActivity();
  return true;
}

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  mappedInput.resetHomeButtonInput();
  if (activity && currentActivity) {
    activity->navigationPrefix = currentActivity->navigationPrefix;
    const auto label = currentActivity->navigationLabel();
    if (!label.empty()) {
      if (!activity->navigationPrefix.empty()) activity->navigationPrefix += "/";
      activity->navigationPrefix += label;
    }
  }
  if (!activity) {
    LOG_ERR("ACT", "Cannot push an unallocated activity");
    return;
  }
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  if (currentActivity) {
    navigationMemory.enter(currentActivity->navigationMemoryKey(), activity->navigationMemoryKey());
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  mappedInput.resetHomeButtonInput();
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::requiresExclusiveStorageLoop() const {
  return currentActivity && currentActivity->requiresExclusiveStorageLoop();
}

bool ActivityManager::isReaderActivity() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->isReaderActivity(); }) ||
         (currentActivity && currentActivity->isReaderActivity());
}

bool ActivityManager::isForegroundReaderActivity() const {
  return pendingAction == PendingAction::None && currentActivity && currentActivity->isReaderActivity();
}

bool ActivityManager::isForegroundActivityManagingTiltSensor() const {
  return pendingAction == PendingAction::None && currentActivity && currentActivity->managesTiltSensor();
}

bool ActivityManager::isForegroundReaderReady() const {
  return isForegroundReaderActivity() && static_cast<ReaderActivity*>(currentActivity.get())->isPageReady();
}

bool ActivityManager::foregroundReaderHoldsRadio() const {
  return isForegroundReaderActivity() && static_cast<ReaderActivity*>(currentActivity.get())->holdsRadio();
}

bool ActivityManager::yieldForegroundReaderForRadio() {
  return !isForegroundReaderActivity() || static_cast<ReaderActivity*>(currentActivity.get())->yieldForRadio();
}

bool ActivityManager::pageTurn(const bool forward) {
  if (!isForegroundReaderReady() || sleepTransition) return false;
  return static_cast<ReaderActivity*>(currentActivity.get())->luotLatTrangNgoai(forward);
}

bool ActivityManager::chapterSkip(const bool forward) {
  if (!isForegroundReaderReady() || sleepTransition) return false;
  return static_cast<ReaderActivity*>(currentActivity.get())->luotNhayChuongNgoai(forward);
}

bool ActivityManager::readerShortcut(const ReaderShortcut shortcut) {
  if (!isForegroundReaderReady() || sleepTransition) return false;
  return static_cast<ReaderActivity*>(currentActivity.get())->requestShortcut(shortcut);
}

bool ActivityManager::handleForcedRefresh() { return currentActivity && currentActivity->handleForcedRefresh(); }

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    return currentActivity->getScreenshotInfo();
  }
  return {};
}

void ActivityManager::requestUpdate(bool immediate) {
  if (immediate) {
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  } else {
    // Deferring the update until current loop is finished
    // This is to avoid multiple updates being requested in the same loop
    requestedUpdate = true;
  }
}
void ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&activityManagerSpinlock);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  xTaskNotify(renderTaskHandle, 1, eIncrement);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

void ActivityManager::requestFirstPaintAndWait() {
  requestedUpdate = false;
  requestUpdateAndWait();
}

// RenderLock

RenderLock::RenderLock(Mode mode) {
  isLocked = xSemaphoreTake(activityManager.renderingMutex, mode == Mode::Try ? 0 : portMAX_DELAY) == pdTRUE;
  assert((mode == Mode::Try || isLocked) && "Blocking render lock acquisition failed");
}

RenderLock::RenderLock() : RenderLock(Mode::Blocking) {}

RenderLock::RenderLock(TryTake) : RenderLock(Mode::Try) {}

RenderLock::RenderLock(Activity&) : RenderLock(Mode::Blocking) {}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

/**
 *
 * Checks if renderingMutex is busy.
 *
 * @return true if renderingMutex is busy, otherwise false.
 *
 */
bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, NULL, 0) != pdTRUE; };

#ifdef TENOR_UI_ACCEPTANCE
void ActivityManager::stepHomeForTest(int direction) {
  RenderLock lock;
  if (currentActivity && currentActivity->name == "Home")
    static_cast<HomeActivity*>(currentActivity.get())->stepForTest(direction);
}

void ActivityManager::tabHomeForTest(int index) {
  // KHONG lay RenderLock o day: HomeActivity::selectTab da lay RenderLock(*this), va renderingMutex duoc
  // tao bang xSemaphoreCreateMutex nen KHONG tai nhap - lay lan hai se treo vinh vien (dung loi da lam
  // vong lap chinh thoi phuc vu lenh serial sau CMD:HOME_TAB). Lay dung MOT lan, tai cho da co san.
  if (currentActivity && currentActivity->name == "Home")
    static_cast<HomeActivity*>(currentActivity.get())->tabForTest(index);
}
#endif
