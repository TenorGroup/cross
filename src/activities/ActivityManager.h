#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "MenuNavigationMemory.h"
#include "components/FrontlightGesture.h"
#include "util/ScreenshotInfo.h"

class Activity;    // forward declaration
namespace tenorchrome {
enum class Zone : uint8_t;
}
class RenderLock;  // forward declaration

// The values ride through a silent restart in RTC memory (main.cpp), so new items go last.
// LIBRARY is the Library screen (library/LibraryListActivity); tenor/cross Home has no entry for
// it, so returning from it lands on the Recent tab.
enum class HomeMenuItem {
  NONE,
  FILE_BROWSER,
  RECENTS,
  OPDS_BROWSER,
  FILE_TRANSFER,
  SETTINGS_MENU,
  STATS_TAB,
  FAVORITES_TAB,
  RECENT_CONTINUE,
  LIBRARY,
  DESK  // tenor/ugly's desk; other shells read it as RECENTS
};

// A reader shortcut a quick action asks the foreground reader for (quickaction::Outcome).
enum class ReaderShortcut : uint8_t { None, Menu, Quote };

/**
 * ActivityManager
 *
 * This mirrors the same concept of Activity in Android, where an activity represents a single screen of the UI. The
 * manager is responsible for launching activities, and ensuring that only one activity is active at a time.
 *
 * It also provides a stack mechanism to allow activities to launch sub-activities and get back the results when the
 * sub-activity is done. For example, the WebServer activity can launch a WifiSelect activity to let the user choose a
 * wifi network, and get back the selected network when the user is done.
 *
 * Main differences from Android's ActivityManager:
 * - No onPause/onResume, since we don't have a concept of background activities
 * - onActivityResult is implemented via a callback instead of a separate method, for simplicity
 */
class ActivityManager {
  friend class RenderLock;

 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity;

  void exitActivity(const RenderLock& lock);

  // Pending activity to be launched on next loop iteration
  std::unique_ptr<Activity> pendingActivity;
  enum class PendingAction { None, Push, Pop, Replace };
  PendingAction pendingAction = PendingAction::None;

  // Task to render and display the activity
  TaskHandle_t renderTaskHandle = nullptr;
  static void renderTaskTrampoline(void* param);
  [[noreturn]] virtual void renderTaskLoop();

  // Set by requestUpdateAndWait(); read and cleared by the render task after render completes.
  // Note: only one waiting task is supported at a time
  TaskHandle_t waitingTaskHandle = nullptr;

  // Mutex to protect rendering operations from race conditions
  // Must only be used via RenderLock
  SemaphoreHandle_t renderingMutex = nullptr;

  // Whether to trigger a render after the current loop()
  // This variable must only be set by the main loop, to avoid race conditions
  std::atomic<bool> requestedUpdate{false};
  bool sleepTransition = false;
  bool homeAfterInput = false;
  HomeMenuItem homeAfterInputTarget = HomeMenuItem::NONE;
  bool toReaderPage = false;
  uint32_t activityGeneration_ = 0;
  MenuNavigationMemory navigationMemory;
  FrontlightGesture lightGesture;
  bool handleLightGesture();
  bool applyLightLevel();
  void deferLightGestureSave();
  void restoreNavigation();
  void saveNavigation(Activity& activity);
  void flushDeferredWrites();

 public:
  explicit ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : renderer(renderer), mappedInput(mappedInput), renderingMutex(xSemaphoreCreateMutex()) {
    assert(renderingMutex != nullptr && "Failed to create rendering mutex");
    stackActivities.reserve(10);
  }
  ~ActivityManager() { assert(false); /* should never be called */ };

  MenuNavigationMemory& menuNavigationMemory() { return navigationMemory; }
  // The name of the screen in front, for the one hook that draws the touch shell's dynamic bar.
  const char* currentName() const;
  HomeMenuItem homeKeyTarget() const;
  void drawLightGesture(const GfxRenderer& renderer) const;
  // The zone the screen in front belongs to, and where the bar's zone icon leads.
  tenorchrome::Zone footZone() const;
  void goZoneRoot();
  // True while the zone icon closes the screens above the book: the book does not reopen its menu.
  bool returningToReaderPage() const { return toReaderPage; }

  HomeMenuItem homeMenuOrigin() const;
  // The Home card the zone icon of the dynamic bar leads to: homeMenuOrigin(), or on tenor/ugly the notebook
  // page under the screen (NONE: the diary).
  HomeMenuItem zoneOrigin() const;
  void begin();
  void loop();

  // Will replace currentActivity and drop all activities on stack
  void replaceActivity(std::unique_ptr<Activity>&& newActivity);

  // goTo... functions are convenient wrapper for replaceActivity()
  void goToFileTransfer();
  void goToUsbDrive();
  // theBanDau: the mo san cua man Cai dat. Man chinh bay cac nhom thanh dong nen no
  // goi thang vao dung the, khoi bat nguoi ta nhay the lai tu dau.
  void goToSettings(int theBanDau = 0);
  void goToFileBrowser(std::string path = {});
  void goToLibrary();
  void goToBrowser();
  void goToReader(std::string path, bool allowFastInitialRefresh = false);
  bool goToSleep(bool fromTimeout = false);
  void goToBoot();
  void goToFullScreenMessage(std::string message, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void goToCrashReport();
  void goHome(HomeMenuItem initialMenuItem = HomeMenuItem::NONE, bool cleanInitialRefresh = false);

  // A write a closing screen can leave for later: the main loop runs it once the next screen's
  // first frame is on the panel, or as the next screen closes, whichever comes first. Sleep closes
  // the screen too, so nothing waits past it. Main task only (onExit).
  void deferWrite(void (*write)());

  // True once the render task has drawn a first frame since boot: work that must not delay the
  // first screen waits for it. Any task.
  bool hasDrawnFrame() const;

  // This will move current activity to stack instead of deleting it
  void pushActivity(std::unique_ptr<Activity>&& activity);
  // The top menu (the light panel): the status strip, the top-edge swipe and the reader's top band.
  void openTopMenu();

  // Remove the currentActivity, returning the last one on stack
  // Note: if popActivity() on last activity on the stack, we will goHome()
  void popActivity();
  bool switchSettingsSibling(int direction);
#ifdef TENOR_UI_ACCEPTANCE
  void stepHomeForTest(int direction);
  void tabHomeForTest(int index);
  uint32_t renderStackHighWaterMark() const;
#endif

  bool preventAutoSleep() const;
  // True while goToSleep() closes the foreground activity for deep sleep.
  bool isSleepTransition() const { return sleepTransition; }
  bool requiresExclusiveStorageLoop() const;
  bool isReaderActivity() const;
  bool isForegroundReaderActivity() const;
  bool isForegroundActivityManagingTiltSensor() const;
  bool isForegroundReaderReady() const;
  bool foregroundReaderHoldsRadio() const;
  bool foregroundReaderCoversPage() const;
  // The foreground reader frees what it can before the radio starts (ReaderActivity::yieldForRadio).
  bool yieldForegroundReaderForRadio();
  // Closes the foreground activity the way leaving it does and writes everything it deferred, so
  // a restart right after reopens the book on the page it showed. Waits out a paint in flight.
  void closeForRestart();
  uint32_t activityGeneration() const { return activityGeneration_; }

  // Queue an external page action on the ready foreground reader. True means
  // this new input was accepted; the reader applies it when rendering is safe.
  bool pageTurn(bool forward);
  // Nhu pageTurn, nhung mot nac CHUONG (giu nut lat trang tren remote BLE).
  bool chapterSkip(bool forward);
  // Ask the ready foreground reader to open its menu or its quote selector on its next
  // pass. False where it cannot (no reader in front, a format without it, a preview).
  bool readerShortcut(ReaderShortcut shortcut);
  bool handleForcedRefresh();
  bool skipLoopDelay() const;
  ScreenshotInfo getScreenshotInfo() const;

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  void requestUpdate(bool immediate = false);

  // Trigger a render and block until it completes.
  // Must NOT be called from the render task or while holding a RenderLock.
  void requestUpdateAndWait();

  // requestUpdateAndWait() for a first screen entered outside loop(): the paint it waits
  // for also draws the update that screen's onEnter() queued, so loop() does not paint the
  // same frame a second time.
  void requestFirstPaintAndWait();
};

extern ActivityManager activityManager;  // singleton, to be defined in main.cpp
