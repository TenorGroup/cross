#ifdef FREEINK_TLS_AUDIT
#include <SecureClient.h>
#include <sys/time.h>

#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/FontDownloadActivity.h"
#include "network/HttpDownloader.h"
#endif
#if defined(TENOR_UI_ACCEPTANCE) || defined(TENOR_PRESS_PROBE)
#include "activities/network/CrossPointWebServerActivity.h"
#endif
#include "activities/settings/OtaUpdateActivity.h"
#include "network/UpdateBoot.h"
#include <Arduino.h>
#include <BlePageTurner.h>
#include <BoardConfig.h>

#include "activities/reader/FirstPaint.h"
#include <Epub.h>
#ifdef TENOR_PRESS_PROBE
#include <Epub/BuildStageProbe.h>
#endif
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <VectorFontSupport.h>
#include <WiFi.h>
#ifndef SIMULATOR
#include <HalGaugeCapacity.h>
#include <Wire.h>  // fuel gauge reads of the probe build; the simulator has no I2C
#endif
#include <XteinkDetect.h>
#include <builtinFonts/all.h>

#include <Memory.h>
#ifdef TENOR_TTF_PROBE
#include <TtfProbe.h>
#endif

#include <cstring>
#ifndef SIMULATOR
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#endif

#include "BlePageTurnerHost.h"
#include "CrossPointSettings.h"
#include "HeapMapProbe.h"
#include "SettingsList.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "QuickAction.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/home/BookStatsActivity.h"
#include "QuoteStore.h"
#include "activities/home/QuotesActivity.h"

// Page turner BLE: lib/BlePageTurner chi chay radio khi capability duoc bat (xem env x3-ble).
#include "FileTransferState.h"
#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
#define CROSSPOINT_BLE_HID_HOST 1
#else
#define CROSSPOINT_BLE_HID_HOST 0
#endif
#include "activities/settings/ClockSyncActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "UIFontTiers.h"
#include "ReaderInkWeight.h"
#include "platform/BootTrial.h"
#include "platform/ColdLog.h"
#include "platform/PutFile.h"
#include "platform/FirmwareProbe.h"
#include "platform/UsbSerialJtagHandoff.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"
#include "util/Timezones.h"
#include "util/WakeBook.h"

#if defined(TENOR_UI_ACCEPTANCE) || defined(TENOR_PRESS_PROBE)
// The serial commands scan integers and words only. newlib's sscanf brings its float scanner and
// strtod (13 KB of flash); siscanf is the same scanner without floats, already linked for the
// time zone. Other C libraries (the simulator) have sscanf alone.
#ifdef _NEWLIB_VERSION
#define SCAN_COMMAND siscanf
#else
#define SCAN_COMMAND sscanf
#endif
#endif

#ifdef FREEINK_TLS_AUDIT
// Diagnostic parent avoids retaining the font manifest or resuming the Home cover.
class TlsAuditActivity final : public Activity {
 public:
  TlsAuditActivity(GfxRenderer& r, MappedInputManager& input) : Activity("TlsAudit", r, input) {}
  void onEnter() override {
    Activity::onEnter();
    {
      RenderLock lock;
      if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
    }
    WiFi.mode(WIFI_STA);
    startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, true, false),
                           [](const ActivityResult&) {});
  }
  bool preventAutoSleep() override { return true; }
  void render(RenderLock&&) override {
    renderer.clearScreen();
    renderer.displayBuffer();
  }
};
#endif

#if CROSSPOINT_VECTOR_FONTS
// Rendering (incl. FreeType TTF rasterization) runs on the Arduino loop task.
// The default 8 KB stack overflows inside FreeType's FT_Open_Face / variable-font
// parsing. This runtime override applies even with the prebuilt (dio_opi) core,
// where CONFIG_ARDUINO_LOOP_STACK_SIZE from sdkconfig is baked in and ignored.
// Vector-font boards only: without TTF the stock loop stack has always sufficed,
// and non-PSRAM boards need the 16KB back in DRAM.
SET_LOOP_TASK_STACK_SIZE(24 * 1024)
#endif

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
static unsigned long allowSleepAt = 0;
static unsigned long lastX4ProPowerClickAt = 0;

namespace {
constexpr unsigned long X4PRO_POWER_DOUBLE_CLICK_MS = 500;
constexpr unsigned long X4PRO_POWER_CLICK_MAX_HOLD_MS = 300;
}  // namespace

// A wake hold must never become an in-app power-button action.  Boot may continue
// while the button is held; swallow the one release that ends that wake gesture.
static bool wakePowerReleasePending = false;

// Fonts
EpdFont notoserif14RegularFont(&notoserif_14_regular);
EpdFont notoserif14BoldFont(&notoserif_14_bold);
EpdFont notoserif14ItalicFont(&notoserif_14_italic);
EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont notoserif12RegularFont(&notoserif_12_regular);
EpdFont notoserif12BoldFont(&notoserif_12_bold);
EpdFont notoserif12ItalicFont(&notoserif_12_italic);
EpdFont notoserif12BoldItalicFont(&notoserif_12_bolditalic);
EpdFontFamily notoserif12FontFamily(&notoserif12RegularFont, &notoserif12BoldFont, &notoserif12ItalicFont,
                                    &notoserif12BoldItalicFont);
EpdFont notoserif16RegularFont(&notoserif_16_regular);
EpdFont notoserif16BoldFont(&notoserif_16_bold);
EpdFont notoserif16ItalicFont(&notoserif_16_italic);
EpdFont notoserif16BoldItalicFont(&notoserif_16_bolditalic);
EpdFontFamily notoserif16FontFamily(&notoserif16RegularFont, &notoserif16BoldFont, &notoserif16ItalicFont,
                                    &notoserif16BoldItalicFont);
EpdFont notoserif18RegularFont(&notoserif_18_regular);
EpdFont notoserif18BoldFont(&notoserif_18_bold);
EpdFont notoserif18ItalicFont(&notoserif_18_italic);
EpdFont notoserif18BoldItalicFont(&notoserif_18_bolditalic);
EpdFontFamily notoserif18FontFamily(&notoserif18RegularFont, &notoserif18BoldFont, &notoserif18ItalicFont,
                                    &notoserif18BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);

#endif  // OMIT_FONTS

// Font he thong: mot ho Geist cho moi co (8, 10, 12; 14 va 16 nam o UIFontTiers.cpp), chu Han tu Noto Sans SC.
EpdFont smallFont(&geist_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&geist_10_regular);
EpdFont ui10BoldFont(&geist_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&geist_12_regular);
EpdFont ui12BoldFont(&geist_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
RTC_NOINIT_ATTR uint32_t silentRebootHomeMenu;
RTC_NOINIT_ATTR uint32_t silentRebootPayload;
// The restart into the update screen (UpdateBoot.h): armed only by silentRestartToOta().
RTC_NOINIT_ATTR update_boot::Slot otaBootSlot;

#ifdef TENOR_PRESS_PROBE
#include <BatteryMonitor.h>
#include <esp_sleep.h>
// CMD:WAKE_TIMER <s>: every later sleep also arms a timed wake, which boots like a
// power-button wake. The RTC clock runs through deep sleep, so the time from the
// wake to the first app instruction is measured, bootloader included.
RTC_NOINIT_ATTR uint32_t probeWakeMagic;
RTC_NOINIT_ATTR uint32_t probeWakeSeconds;
// CMD:WAKE_HOLD <ms>: the next timed wakes read the power key as held until this many ms after the
// app started (0 clears it), so the hold check runs as it does for a real press.
RTC_NOINIT_ATTR uint32_t probeWakeHoldMagic;
RTC_NOINIT_ATTR uint32_t probeWakeHoldMs;
RTC_NOINIT_ATTR uint64_t probeWakeTargetUs;
constexpr uint32_t PROBE_WAKE_MAGIC = 0x57414B45;
extern "C" uint64_t esp_rtc_get_time_us(void);
// CMD:KEEP_HEAP 1: the next Wi-Fi exits to Home end the session in place instead of the
// silent restart, and report the heap they leave, to decide whether the restart is still needed.
static bool probeKeepHeap = false;
// CMD:OTA_DRYRUN: the test manifest the series of update boots reads.
RTC_NOINIT_ATTR char probeDryRunUrl[160];
// The last dry run's result line, printed again by the boot after it (the restart drops the port).
constexpr uint32_t PROBE_DRYRUN_LINE_MAGIC = 0x4C494E45;
RTC_NOINIT_ATTR uint32_t probeDryRunLineMagic;
RTC_NOINIT_ATTR char probeDryRunLine[640];
void probeKeepDryRunLine(const char* line) {
  snprintf(probeDryRunLine, sizeof(probeDryRunLine), "%s", line);
  probeDryRunLineMagic = PROBE_DRYRUN_LINE_MAGIC;
}
// Three times, 3, 6 and 10 s after boot, whenever the loop runs: the cable reader reattaches late.
static void probeReprintDryRunLine() {
  static uint8_t printed = 0;
  static constexpr unsigned long AT_MS[] = {3000, 6000, 10000};
  if (probeDryRunLineMagic != PROBE_DRYRUN_LINE_MAGIC || printed >= 3 || millis() < AT_MS[printed]) return;
  probeDryRunLine[sizeof(probeDryRunLine) - 1] = '\0';
  logSerial.printf("OTA_DRYRUN_PREV %u/3 %s", static_cast<unsigned>(printed + 1), probeDryRunLine);
  if (++printed == 3) probeDryRunLineMagic = 0;
}
#if FREEINK_CAP_TOUCH && !defined(SIMULATOR)
#include "platform/TouchProbe.h"
// CMD:TAP / CMD:SWIPE: a scripted finger, fed to the GT911 read on this loop task in the app's coordinates.
static touchprobe::Plan probeTouchPlan;
static int8_t probeTouchHook(uint16_t& x, uint16_t& y) {
  int ax = 0, ay = 0;
  const int8_t state = touchprobe::sample(probeTouchPlan, millis(), ax, ay);
  if (state > 0) {
    const auto& t = BoardConfig::ACTIVE.touch;
    touchprobe::toTouch(ax, ay, renderer.getOrientation(), renderer.getDisplayWidth(), renderer.getDisplayHeight(),
                        t.rawMaxX - t.rawMinX, t.rawMaxY - t.rawMinY, x, y);
  }
  return state;
}
#endif
#endif
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;
constexpr uint32_t SILENT_REBOOT_TARGET_SETTINGS = 2;
constexpr uint32_t SILENT_REBOOT_TARGET_OTA = 3;
constexpr uint32_t SILENT_REBOOT_TARGET_MAX = SILENT_REBOOT_TARGET_OTA;
constexpr uint32_t SILENT_REBOOT_LIGHT_ON = 1U << 0;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,          // cold boot, flash, panic, or plain reboot
  Silent,          // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  SplashlessWake,  // wake from deep sleep with the splash suppressed by the SD flag
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

// A silent restart is internal maintenance, so the light must come back exactly
// as the user left it. SETTINGS.frontlightOn is the saved preference and
// legitimately diverges from the live state (a wake with Restore Light on Wake
// off leaves the light off while the saved "was on" preference is kept), so
// carry the live state across the reboot instead of re-deriving it from
// settings. Cleared with the magic in setup(). A restart to Home also keeps the
// Home tab it came from.
static void armSilentReboot(const uint32_t target) {
  silentRebootTarget = target;
  if (target == SILENT_REBOOT_TARGET_HOME)
    silentRebootHomeMenu = static_cast<uint32_t>(activityManager.homeMenuOrigin());
  silentRebootPayload = Frontlight.isOn() ? SILENT_REBOOT_LIGHT_ON : 0;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
}

// Returns instead of rebooting when sleep supersedes the reboot; callers keep
// running in that case.
static void silentRestartTo(const uint32_t target, const char* targetName) {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  armSilentReboot(target);
  LOG_DBG("MAIN", "Silent restart (target=%s)", targetName);
  // E-ink retains the previous frame until the target's first paint lands
  // (~2-3s). Without an overlay, users don't see the reboot and fire input
  // through to the new activity. On Home, Select on the default
  // selectorIndex=0 opens the most-recent book, looking like a trampoline back
  // to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestart() {
#ifdef TENOR_PRESS_PROBE
  if (!deepSleepInProgress && probeKeepHeap) {
    WiFi.mode(WIFI_OFF);
    delay(100);
    LOG_INF("PROBE", "Wi-Fi off without restart heap=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return;
  }
#endif
  silentRestartTo(SILENT_REBOOT_TARGET_HOME, "home");
}

void silentRestartToReader() { silentRestartTo(SILENT_REBOOT_TARGET_READER, "reader"); }

void silentRestartToSettings() { silentRestartTo(SILENT_REBOOT_TARGET_SETTINGS, "settings"); }

void silentRestartToOta(const uint8_t dryRunsLeft, const uint8_t dryRunsTotal) {
  if (deepSleepInProgress) return;
  update_boot::arm(otaBootSlot, dryRunsLeft, dryRunsTotal);
  silentRestartTo(SILENT_REBOOT_TARGET_OTA, "ota");
}

#if CROSSPOINT_BLE_HID_HOST
static void runQuickAction(uint8_t action, quickaction::Trigger trigger);

// The page turner's two doors that only this file has: a remote shortcut runs the catalog
// action of the same name, and a heap in pieces restarts into the open book.
static void runRemoteShortcut(const bleturner::Action shortcut) {
  runQuickAction(shortcut == bleturner::Action::ReaderMenu ? CrossPointSettings::READER_MENU
                                                           : CrossPointSettings::SAVE_QUOTE,
                 quickaction::Trigger::Remote);
}
// The close waits out a paint and writes the reading place and every deferred write first.
static void restartIntoOpenBook() {
  activityManager.closeForRestart();
  silentRestartToReader();
  // Only a board that keeps its rails (touch) gets here without a restart: reopen the book.
  activityManager.goToReader(APP_STATE.openEpubPath);
}
#endif

void restartToHomeAfterStorageHandoff() {
  if (deepSleepInProgress) return;  // sleeping supersedes the storage handoff reboot
  armSilentReboot(SILENT_REBOOT_TARGET_HOME);
  LOG_DBG("MAIN", "Restart after storage handoff (target=home)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  handoffUsbOtgToSerialJtag();
  ESP.restart();
}

void toggleFrontlight() {
  if (!Frontlight.present()) return;
  const bool lightOn = !Frontlight.isOn();
  Frontlight.setOn(lightOn);
  SETTINGS.frontlightOn = lightOn ? 1 : 0;
  SETTINGS.saveToFile();
  LOG_INF("LIGHT", "Frontlight toggled %s", lightOn ? "on" : "off");
}

bool handleX4ProFrontlightDoubleClick() {
  if (!BoardConfig::isX4Pro() || !SETTINGS.doubleClickPwrLight || !gpio.wasReleased(HalGPIO::BTN_POWER)) {
    return false;
  }

  const unsigned long now = millis();
  if (gpio.getPowerButtonHeldTime() > X4PRO_POWER_CLICK_MAX_HOLD_MS) {
    lastX4ProPowerClickAt = 0;
    return false;
  }

  if (lastX4ProPowerClickAt == 0 || now - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = now;
    return false;
  }

  lastX4ProPowerClickAt = 0;
  toggleFrontlight();
  return true;
}

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  const size_t written = file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
  if (written != renderer.getBufferSize()) {
    LOG_ERR("SLP", "Incomplete sleep frame: %u bytes", static_cast<unsigned>(written));
    Storage.remove(SLEEP_FRAME_FILE);
  }
}

static bool loadSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  // Kept: every sleep rewrites or removes it before the flag that reads it is cleared again, and a
  // wake cut off before its first frame retries from the same frame. Deleting it was a card write
  // on the wake path.
  return true;
}

// Home's first frame after a wake writes the re-armed splash through this one copy of the store's
// save, which setup() and the sleep path already link.
void saveAppState() { APP_STATE.saveToFile(); }

static void sleepUntilPowerButton() {
#ifndef SIMULATOR
  const bool preserveClock = halClock.hasValidTime();
  if (gpio.deviceIsX4()) {
    LOG_INF("SLP", "X4 clock retention requested=%u", preserveClock);
  }
  powerManager.startDeepSleep(gpio, preserveClock);
#else
  powerManager.startDeepSleep(gpio);
#endif
}

static bool autoSleepBlockedUntilInput = false;

// Enter deep sleep mode
void enterDeepSleep(bool fromTimeout = false) {
  const uint32_t sleepStarted = millis();
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  const bool isQuickResumeSleep =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  // Every sleep mode leaves a complete retained frame on the e-ink panel. Keep
  // it visible until the first useful reader or home paint replaces it.
  APP_STATE.showBootScreen = false;

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  const bool asleep = activityManager.goToSleep(fromTimeout);
  if (asleep) LOG_INF("SLP", "Timing image-ready=%lu ms", static_cast<unsigned long>(millis() - sleepStarted));
  // One write of the state, once the sleep screen is up: what that screen chose (the quote shown,
  // the picture) goes in this write instead of a second write of the same file of its own.
  APP_STATE.saveToFile();
  LOG_INF("SLP", "Timing save-state=%lu ms", static_cast<unsigned long>(millis() - sleepStarted));
  if (!asleep) {
    deepSleepInProgress = false;
    autoSleepBlockedUntilInput = true;
    return;
  }
  const uint32_t retainedStarted = millis();

  // The X3 UC8279 wakes by refreshing only the pixels that differ from the frame kept here, so it
  // keeps the frame only when that frame is what the glass shows: not after a gray pass, whose
  // levels no B/W frame names. Without a frame the wake drives every pixel instead.
  // The tenor/ugly doodle is a black and white frame kept the same way.
  const bool tenorScreen = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TENOR ||
                           SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::UGLY;
  if (renderer.diffOnlyPanel() ? renderer.panelFrameKnown() : isQuickResumeSleep || tenorScreen) {
    saveSleepFrameBuffer();
  } else if (Storage.exists(SLEEP_FRAME_FILE)) {
    Storage.remove(SLEEP_FRAME_FILE);
  }

  LOG_INF("SLP", "Timing retained-frame=%lu ms", static_cast<unsigned long>(millis() - retainedStarted));
  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  halTiltSensor.deepSleep();
#ifndef SIMULATOR
  halGaugeCapacity.abandon();
#endif
  display.deepSleep();
#if TENOR_COLD_LOG
  coldlog::flush("sleep");
#endif
  Storage.prepareForDeepSleep();
  LOG_INF("SLP", "Timing ready-to-sleep=%lu ms", static_cast<unsigned long>(millis() - sleepStarted));
  LOG_DBG("MAIN", "Entering deep sleep");
#ifdef TENOR_PRESS_PROBE
  if (probeWakeMagic == PROBE_WAKE_MAGIC && probeWakeSeconds > 0) {
    const uint64_t us = static_cast<uint64_t>(probeWakeSeconds) * 1000000ULL;
    esp_sleep_enable_timer_wakeup(us);
    probeWakeTargetUs = esp_rtc_get_time_us() + us;
  }
#endif

  sleepUntilPowerButton();
}

// Heap ledger: one line per boot milestone and per screen change, so the
// 189 KB in use at Home can be attributed instead of guessed (M0, 18/09/2026).
static void logHeapMark(const char* tag) {
  const auto heap = HalMemory::getInternalHeap();
  LOG_INF("HEAP", "%s free=%u largest=%u min=%u", tag, static_cast<unsigned>(heap.freeBytes),
          static_cast<unsigned>(heap.largestBlockBytes), static_cast<unsigned>(heap.minFreeBytes));
}

// sdFonts false: the update boot, whose screen draws built-in fonts only (UpdateBoot.h).
void setupDisplayAndFonts(bool seamless = false, bool sdFonts = true) {
#if !FREEINK_MCU_C3
  // C3 resolves its controller in HalGPIO::begin() before SPI claims the
  // display pins. X4 Pro skips that C3-only path, so probe here before
  // display.begin() selects and initializes its panel driver.
  static bool controllerResolved = false;
  if (!controllerResolved) {
    controllerResolved = true;
    if (freeink::applyXteinkDisplayController()) {
      LOG_DBG("MAIN", "Panel controller: UltraChip UC81xx variant detected");
    }
  }
#endif

  display.begin(seamless);
  logHeapMark("display.begin");
  renderer.begin();
  activityManager.begin();
  logHeapMark("render-task");
  LOG_DBG("MAIN", "Display initialized");

  // The render worker already exists. Keep all font registration and the
  // persisted UI tier in one locked boot transaction before the first activity.
  RenderLock fontLock;

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12FontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, notoserif16FontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18FontFamily);

  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(UI_TITLE_FONT_ID, uiFontTierFamily(UIFontRole::Title, 0));
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  logHeapMark("fonts-builtin");
  // tenor/ugly: its four fonts always go in, under this lock like the others, so no screen registers them while
  // the render task reads the font table (the box that asks before the shell turns to tenor/ugly is drawn from
  // tenor/cross). Four table entries, no glyph data: the faces live in flash.
  ugly::ensureFonts(renderer);

  // Discover and load SD card fonts
  if (sdFonts) sdFontSystem.begin(renderer);
  if (!applyUiFontSize(renderer, SETTINGS.uiTextSize)) {
    LOG_ERR("MAIN", "Unable to apply saved UI font size");
  }
  logHeapMark("fonts-sd");

  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
  BoardConfig::holdPowerRails();

#ifdef ENABLE_SERIAL_LOG
#ifdef CROSSPOINT_WAIT_FOR_USB_SERIAL
  // Development builds preserve reliable early CDC logs; release builds let
  // enumeration proceed asynchronously so users do not pay this startup cost.
  delay(250);
#endif
#ifdef TENOR_PRESS_PROBE
  logSerial.setRxBufferSize(8192);  // CMD:PUT: a 4096 byte window must fit while the card is written
#endif
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#if ARDUINO_USB_CDC_ON_BOOT && defined(ARDUINO_USB_MODE) && !ARDUINO_USB_MODE
  // Native USB CDC reboots into ROM download on a host's DTR/RTS reset sequence; a board that
  // cannot be reached in download mode would stay there until reset by hand.
  logSerial.enableReboot(false);
#endif
#endif
#if FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)
  boot_trial::begin();
#endif
#if defined(TENOR_PRESS_PROBE) && FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)
  fwprobe::onBoot();  // CMD:ROLLBACK_TEST crashes the boots it was armed for, here
#endif

  HalSystem::begin();
  // checkPanic() clears the watchdog capture marker after a successful SD
  // dump, so retain the boot classification for the later activity route.
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too - RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_MAX) ? silentRebootTarget : 0;
  const bool silentRebootLightOn = isSilentReboot && (silentRebootPayload & SILENT_REBOOT_LIGHT_ON) != 0;
  const HomeMenuItem snapshotHomeMenu =
      isSilentReboot && silentRebootHomeMenu <= static_cast<uint32_t>(HomeMenuItem::FAVORITES_TAB)
          ? static_cast<HomeMenuItem>(silentRebootHomeMenu)
          : HomeMenuItem::NONE;
  silentRebootMagic = 0;
  silentRebootTarget = 0;
  silentRebootHomeMenu = 0;
  silentRebootPayload = 0;
  // Spent here on every boot, like the flags above: whatever ends the update boot, the next one
  // is a normal boot unless the update screen arms another.
  const update_boot::Request otaBoot = update_boot::take(otaBootSlot);
  const bool updateBoot = isSilentReboot && snapshotTarget == SILENT_REBOOT_TARGET_OTA && otaBoot.armed;

#ifdef TENOR_PRESS_PROBE
  const bool probeTimerWake = esp_reset_reason() == ESP_RST_DEEPSLEEP &&
                              esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER &&
                              probeWakeMagic == PROBE_WAKE_MAGIC;
  if (probeTimerWake) {
    LOG_INF("PROBE", "timer wake pre-app=%ld ms",
            static_cast<long>((static_cast<int64_t>(esp_rtc_get_time_us()) - static_cast<int64_t>(probeWakeTargetUs)) /
                              1000) -
                static_cast<long>(millis()));
  }
#endif
  gpio.begin();
  powerManager.begin();
  logHeapMark("boot");

  const auto wakeupReason = gpio.getWakeupReason();
  // Sample the wake hold now - a click wake is released within milliseconds of
  // boot - but defer the sleep-or-boot decision until SETTINGS is loaded below:
  // click-to-wake is a setting, and an X4 battery power-off cuts all power, so
  // only SD state survives to the next boot.
  // The hold that wakes is watched from here, while the stores, the display and the fonts load: it
  // is judged early where the settings can tell (below), and for good just before the first thing
  // that reaches the glass or the card.
  bool watchWakeHold = wakeupReason == HalGPIO::WakeupReason::PowerButton;
#ifdef TENOR_PRESS_PROBE
  const bool probeHold = probeTimerWake && probeWakeHoldMagic == PROBE_WAKE_MAGIC && probeWakeHoldMs > 0;
  // A plain timed wake stands in for a hold already verified: nothing to watch.
  if (probeTimerWake && !probeHold) watchWakeHold = false;
  if (probeHold) {
    gpio.setProbeWakeHold(probeWakeHoldMs);
    LOG_INF("PROBE", "wake hold simulated until=%lu ms", static_cast<unsigned long>(probeWakeHoldMs));
  }
#endif
  [[maybe_unused]] const unsigned long holdBegan = millis();
  if (watchWakeHold) gpio.beginPowerWakeHold();

  // X4 Pro and X4 Classic both map BTN_UP to GPIO0 - an ESP32-S3 boot strap - so
  // gate recovery on the non-strap Down key (GPIO7) to avoid a stuck-in-recovery loop.
  const auto recoveryButton = (BoardConfig::isX4Pro() || BoardConfig::isX4Classic()) ? MappedInputManager::Button::Down
                                                                                     : MappedInputManager::Button::Up;
  const bool recoveryFirmwareMode = wakeupReason == HalGPIO::WakeupReason::PowerButton && !BoardConfig::isPaperMono() &&
                                    gpio.isPressed(HalGPIO::BTN_POWER) && mappedInputManager.isPressed(recoveryButton);

  halTiltSensor.begin();
  halClock.begin();

#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");
#else
  LOG_INF("MAIN", "Device: %s", BoardConfig::ACTIVE.name);
#endif

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    if (watchWakeHold) gpio.endPowerWakeHold(false);
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  HalSystem::checkPanic();
  logHeapMark("storage");

  APP_STATE.loadFromFile();
  const bool isSleepWake = wakeupReason == HalGPIO::WakeupReason::PowerButton;
  const bool isPersistedSleepWake = isSleepWake && !APP_STATE.showBootScreen;

  if (recoveryFirmwareMode) {
    LOG_INF("MAIN", "Recovery firmware mode (%s + POWER held at boot)",
            (BoardConfig::isX4Pro() || BoardConfig::isX4Classic()) ? "DOWN" : "UP");
  }

  // Touch boards default the reader menu to the toolbar overlay instead of the
  // full-screen list. Seeded before the load: fromJson() falls back to the
  // in-memory value only when the file carries no readerMenuStyle key, so a
  // user's saved choice (either style) still wins. The X4 Pro opens the list menu, as the X3 does.
  if (gpio.hasTouch() && !FREEINK_DEVICE_X4PRO) {
    SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  }
  SETTINGS.loadFromFile();
#if CROSSPOINT_BLE_HID_HOST
  beginPageTurner(renderer, SETTINGS.ble, runRemoteShortcut, restartIntoOpenBook);
#endif
  logHeapMark("store-settings");
  // Push the saved timezone's POSIX rule into the clock (migrating the legacy
  // UTC-offset setting on first boot after the update).
  timezones::applyToClock();
  shell::expireIfOver();  // tenor/ugly is a limited edition: past its last day the device is tenor/cross again
  RECENT_BOOKS.loadFromFile();
  logHeapMark("store-recent");
  // Quote files written before v1.0.11 are renamed to the per-book scheme here, before the
  // first book opens: the reader finds a book's highlights by file name alone, so an old
  // name would hide them until the Quotes screen was visited. A no-op once done, apart from
  // repairing a quote edit a power cut interrupted.
  quotes::migrateNames();
  logHeapMark("store-quotes");
  READING_STATS.loadFromFile();
  logHeapMark("store-stats");
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  logHeapMark("i18n");
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);
  logHeapMark("stores");

  // Brightness and warmth are always restored. A normal wake starts with the
  // light off unless Restore Light on Wake is enabled; silent maintenance
  // reboots replay the live state captured at restart, so they neither go dark
  // nor light up against the user's wake preference.
  const bool restoreLightOn =
      isSilentReboot ? silentRebootLightOn : (SETTINGS.frontlightOn != 0 && SETTINGS.frontlightRestoreOnWake != 0);
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth, restoreLightOn);

  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton: {
      // With Short Power Button Press = Sleep, a single click wakes on any
      // device, X3 included, so the tap that locks also unlocks; otherwise the
      // button must still be held (ghost-wake debounce).
      const bool heldSoFar = !watchWakeHold || gpio.powerWakeHeld();
#ifdef TENOR_PRESS_PROBE
      LOG_INF("PROBE", "wake hold begin=%lu early=%lu held=%u", holdBegan, millis(), heldSoFar);
#endif
      if (!CrossPointSettings::acceptPowerWake(SETTINGS.shortPwrBtn, heldSoFar)) {
        LOG_DBG("MAIN", "Power-button wake not held through verification, sleeping");
        halTiltSensor.deepSleep();
        Storage.prepareForDeepSleep();
        sleepUntilPowerButton();
      }
      wakePowerReleasePending = true;
      break;
    }
    case HalGPIO::WakeupReason::AfterUSBPower:
      // Most devices return to sleep after a USB-powered cold boot.
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_X4CLASSIC || FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_EEGO_A4
      // X4 Pro must stay awake so USB Serial/JTAG remains available after leaving
      // USB Drive and reconnecting the cable. Paper Mono has no armable GPIO wake
      // (its button is behind the PMIC). EEGO A4's post-flash reset reads as
      // POWERON (native-USB), so a flash would otherwise be misclassified as a
      // USB-power cold boot and sleep. Sleeping any of these here would strand
      // the device in a USB-replug boot loop (or sleep right after a flash).
      break;
#else
      halTiltSensor.deepSleep();
      Storage.prepareForDeepSleep();
      sleepUntilPowerButton();
      break;
#endif
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  LOG_INF("MAIN", "Starting tenor/cross version " CROSSPOINT_VERSION);
#ifndef SIMULATOR
  // A USB write to app0 does not change the OTA boot selection. Report the
  // running partition and ELF identity before accepting a hardware test.
  const esp_partition_t* runningApp = esp_ota_get_running_partition();
  esp_app_desc_t runningDesc{};
  if (runningApp && esp_ota_get_partition_description(runningApp, &runningDesc) == ESP_OK) {
    char elfSha[65];
    for (size_t i = 0; i < sizeof(runningDesc.app_elf_sha256); ++i) {
      snprintf(elfSha + i * 2, 3, "%02x", runningDesc.app_elf_sha256[i]);
    }
    LOG_INF("BOOT", "Firmware %s partition=%s address=0x%06x elf_sha256=%s", CROSSPOINT_VERSION,
            runningApp->label, static_cast<unsigned>(runningApp->address), elfSha);
  }
#endif

  // Resolve the boot presentation. A splashless wake cleans the retained sleep
  // image on its first useful paint; subsequent paints use the normal cadence.
  // Only a verified deep-sleep wake may use the one-shot persisted flag.
  // Otherwise a stale flag could suppress the splash on a cold boot.
  const BootResume resume = isSilentReboot         ? BootResume::Silent
                            : isPersistedSleepWake ? BootResume::SplashlessWake
                                                   : BootResume::Splash;
  bool needsWakeRefresh = false;
  // A crash may have come from the book that was open: the next wake does not reopen it.
  if (rebootedFromPanic && !APP_STATE.openEpubPath.empty()) {
    APP_STATE.openEpubPath.clear();
    APP_STATE.saveToFile();
  }
  const std::string wakeBook =
      wakebook::bookToOpen(isSleepWake, SETTINGS.wakeIntoBook, APP_STATE.openEpubPath, RECENT_BOOKS.getBooks(),
                           [](const std::string& path) { return Storage.exists(path.c_str()); });
  const bool wakeToBook = !wakeBook.empty();

  setupDisplayAndFonts(resume != BootResume::Splash, !updateBoot);
  renderer.setDiffOnlyPanel(gpio.deviceIsX3() && display.getController() == HalDisplay::Controller::UC8279);
  logHeapMark("display-and-fonts");

  bool sleepFrameKept = false;
  if (resume == BootResume::SplashlessWake) {
    // One-shot flag: re-arm the splash for the next ordinary boot. Set here,
    // written once the first frame is up (HomeActivity::render); every other
    // first screen writes it now, before it paints. Until then the card still
    // says "asleep", so a wake cut off before its first frame (power lost, a
    // hang) is simply repeated from the same kept frame. Writing in memory
    // first means a sleep started meanwhile clears it again, and whichever
    // save runs last writes that. A crash reboot shows the splash and
    // re-arms the flag below.
    APP_STATE.showBootScreen = true;
    // The kept frame goes back into the controller while the hold is still being watched, so the
    // first paint can start the moment the hold is judged. This reaches neither the glass nor the card.
    const uint32_t wakeStarted = millis();
    if (Storage.exists(SLEEP_FRAME_FILE) && loadSleepFrameBuffer()) {
      sleepFrameKept = true;
      if (gpio.deviceIsX3()) {
        // Restore controller RAM without activating a waveform. The first
        // Home/Reader paint cleans directly from this retained sleep frame.
        renderer.cleanupGrayscaleWithFrameBuffer();
      }
      LOG_DBG("MAIN", "Restored sleep frame baseline");
    } else {
      // Nothing says what the glass holds: the first paint drives every pixel.
      renderer.redriveNextRefresh();
    }
    LOG_INF("BOOT", "Wake frame=%lu ms", static_cast<unsigned long>(millis() - wakeStarted));
  }

  if (watchWakeHold) {
    // The hold's last word, before anything reaches the glass or the card. With the quick press set
    // to Sleep a tap is enough to wake, so nothing waits for the window.
    [[maybe_unused]] const unsigned long finalAt = millis();
    const bool held = gpio.endPowerWakeHold(SETTINGS.shortPwrBtn != CrossPointSettings::SLEEP);
#ifdef TENOR_PRESS_PROBE
    LOG_INF("PROBE", "wake hold final=%lu wait=%lu held=%u", finalAt, millis() - finalAt, held);
#endif
    if (!CrossPointSettings::acceptPowerWake(SETTINGS.shortPwrBtn, held)) {
      LOG_DBG("MAIN", "Power-button wake not held through the window, sleeping");
      halTiltSensor.deepSleep();
      display.deepSleep();
      Storage.prepareForDeepSleep();
      sleepUntilPowerButton();
    }
  }

  // A wake on its way to Home, with the hold just judged and the "Wake-up notice" setting on (off by
  // default): the sleep screen stays and takes the notice the going-to-sleep one has, in one fast
  // refresh over the kept frame that the first screen then cleans. Not with the quick press on Sleep
  // (no window was waited out, so the notice would only delay Home). Off, nothing here runs.
  const bool wakeNotice = SETTINGS.wakeNotice != 0 && watchWakeHold && renderer.diffOnlyPanel() &&
                          SETTINGS.shortPwrBtn != CrossPointSettings::SLEEP && !recoveryFirmwareMode &&
                          !rebootedFromPanic && !updateBoot && !wakeToBook;
  // Not without the frame (what the glass shows is not known).
  if (wakeNotice && sleepFrameKept) {
#ifdef TENOR_PRESS_PROBE
    LOG_INF("BOOT", "Wake label kick=%lu", static_cast<unsigned long>(millis()));
#endif
    SleepActivity::showStartingUp(renderer);
#ifdef TENOR_PRESS_PROBE
    LOG_INF("BOOT", "Wake label started=%lu", static_cast<unsigned long>(millis()));
#endif
  }
#ifdef TENOR_WAKE_LABEL_GRAY
  // The same for a sleep screen the card keeps no frame of (a gray one): off unless the build asks.
  if (wakeNotice && resume == BootResume::SplashlessWake && !sleepFrameKept) {
    SleepActivity::showStartingUpOverGray(renderer);
  }
#endif

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::SplashlessWake:
      if (recoveryFirmwareMode || rebootedFromPanic || wakeToBook) APP_STATE.saveToFile();
      needsWakeRefresh = true;
      break;
    case BootResume::Splash:
      if (!APP_STATE.showBootScreen) {
        APP_STATE.showBootScreen = true;
        APP_STATE.saveToFile();
      }
      // Whatever the glass kept from before this start is unknown to the controller.
      renderer.redriveNextRefresh();
      activityManager.goToBoot();
      break;
  }

  // Output polarity is resolved per render by ActivityManager (night mode
  // inverts only the reading surfaces), so nothing to restore here.

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (rebootedFromPanic) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (updateBoot) {
    // Straight to the update, on the heap of a fresh boot; the screen restarts on its way out.
    auto update = makeUniqueNoThrow<OtaUpdateActivity>(renderer, mappedInputManager, otaBoot);
#ifdef TENOR_PRESS_PROBE
    probeDryRunUrl[sizeof(probeDryRunUrl) - 1] = '\0';
    if (update && otaBoot.dryRun()) update->setDryRun(probeDryRunUrl);
#endif
    if (update)
      activityManager.replaceActivity(std::move(update));
    else
      activityManager.goHome();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_SETTINGS) {
    // Back out of the WiFi rows and the user is where they left off, not on Home.
    activityManager.goToSettings();
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home - don't fall
    // through to the wake-into-book branch below.
    activityManager.goHome(snapshotHomeMenu);
  } else if (wakeToBook) {
    // Wake straight into the book last opened. The reader's
    // first paint is a cleaning waveform (allowFastInitialRefresh stays false),
    // which is the pass that takes the retained sleep frame off the panel.
    activityManager.goToReader(wakeBook);
  } else {
    activityManager.goHome(HomeMenuItem::RECENT_CONTINUE, needsWakeRefresh);
  }

  if (resume == BootResume::Silent) {
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestFirstPaintAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

#ifndef SIMULATOR
  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &otaState) == ESP_OK &&
      otaState == ESP_OTA_IMG_PENDING_VERIFY) {
    // SD, settings and activity startup succeeded. Wait for the first physical paint.
    activityManager.requestUpdateAndWait();
#if FREEINK_DEVICE_X4PRO
    // Back and Confirm live on the touch controller: an image that cannot bring it up leaves the
    // unit without a way to reach the SD installer. Stay on trial; the watchdog falls back.
    if (!gpio.hasTouch()) {
      LOG_ERR("OTA", "Boot self-check: no touch controller, left on trial");
    } else
#endif
    {
      const esp_err_t verified = esp_ota_mark_app_valid_cancel_rollback();
#if FREEINK_DEVICE_X4PRO
      boot_trial::passed();
#endif
      LOG_INF("OTA", "Boot self-check complete: %s", esp_err_to_name(verified));
    }
  }
#endif
  allowSleepAt = millis() + 2000;
}

#if defined(TENOR_UI_ACCEPTANCE) || defined(TENOR_PRESS_PROBE)
template <typename Visitor>
static bool visitDiagnosticSetting(const String& key, Visitor&& visitor) {
  if (key == "blePageTurnerEnabled") {
    SettingInfo info;
    info.key = "blePageTurnerEnabled";
    info.valueGetter = []() -> uint8_t { return SETTINGS.ble.enabled; };
    info.valueSetter = [](const uint8_t v) { SETTINGS.ble.enabled = v; };
    return visitor(info);
  }
  if (key == "fontFamily") return visitor(buildFontFamilySetting(&sdFontSystem.registry()));
  if (key == "fontSize") return visitor(buildFontSizeSetting(&sdFontSystem.registry()));
  for (const auto& info : getBaseSettingsList()) {
    if (info.key != nullptr && key.equals(info.key)) return visitor(info);
  }
  return false;
}
#endif

static void updateTiltSensorForForegroundActivity(const bool foregroundReader,
                                                   const bool foregroundActivityManagesTiltSensor) {
  halTiltSensor.setStrength(SETTINGS.tiltStrengthH, SETTINGS.tiltStrengthV);
  halTiltSensor.configureShake(SETTINGS.shakeAction, SETTINGS.shakeStrength);
  halTiltSensor.configureFlip(SETTINGS.faceDownAction, SETTINGS.faceUpAction);
  halTiltSensor.configureDoubleTap(SETTINGS.doubleTapAction, SETTINGS.screenTapAction, SETTINGS.edgeTapAction);
  // Menus wait for a side flick to come back (picking the device up is not a tab
  // step); the reader keeps page turns immediate.
  halTiltSensor.confirmSideFlicks(!foregroundReader);
  if (foregroundReader) {
    // Row tilt belongs to the menu screens: the reader keeps the page-turn axis
    // and nothing else, so the vertical channel is disarmed on the way in.
    halTiltSensor.configureVerticalGesture(CrossPointTiltPageTurn::TILT_OFF, false);
    halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, true);
  } else if (!foregroundActivityManagesTiltSensor) {
    halTiltSensor.configureVerticalGesture(CrossPointTiltPageTurn::TILT_OFF, false);
    halTiltSensor.update(CrossPointTiltPageTurn::TILT_OFF, SETTINGS.orientation, false);
  }
}

// Runs a short action on the screen in front, or nothing where it has no meaning there.
#if defined(TENOR_TAP_LOG) && !defined(SIMULATOR)
// Measurement build only: every double tap the gestures act on, and one line a minute awake, kept in
// RAM and rewritten whole to /v1019/tap/<boot>-<part>.csv. Worn with no tap made on purpose, every
// tap line is a false one and the minute lines are the hours it was measured over.
static void tapLog(const char* const what) {
  static std::string lines;
  static unsigned part = 0;
  static long boot = 0;
  if (boot == 0) boot = static_cast<long>(time(nullptr));
  char row[96];
  snprintf(row, sizeof(row), "%ld,%lu,%s,%s\n", static_cast<long>(time(nullptr)), millis(), what,
           activityManager.isReaderActivity() ? "doc" : "khac");
  lines += row;
  Storage.mkdir("/v1019");
  Storage.mkdir("/v1019/tap");
  char path[64];
  snprintf(path, sizeof(path), "/v1019/tap/%ld-%u.csv", boot, part);
  HalFile file;
  if (Storage.openFileForWrite("TLOG", path, file)) {
    file.print("epoch,ms,su_kien,man\n");
    file.write(reinterpret_cast<const uint8_t*>(lines.data()), lines.size());
    file.close();
  }
  if (lines.size() > 6000) {
    lines.clear();
    part++;
  }
}

static void tapLogTick() {
  static unsigned long last = 0;
  if (last != 0 && millis() - last < 60000) return;
  last = millis();
  tapLog("phut");
}
#endif

// The side button that turns the page forward (or back), as the side button layout sets it.
[[maybe_unused]] static uint8_t sideButton(const bool forward) {
  const bool swapped = SETTINGS.sideButtonLayout == CrossPointSettings::NEXT_PREV;
  return forward != swapped ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP;
}

static void runQuickAction(const uint8_t action, const quickaction::Trigger trigger) {
#if defined(TENOR_TAP_LOG) && !defined(SIMULATOR)
  if (trigger == quickaction::Trigger::DoubleTap) tapLog("lung");
  if (trigger == quickaction::Trigger::ScreenTap) tapLog("man-hinh");
  if (trigger == quickaction::Trigger::EdgeTap) tapLog("canh");
#endif
  // Long enough for two 10 ms samples to agree, short of any hold action.
  [[maybe_unused]] static constexpr uint16_t QUICK_PRESS_HOLD_MS = 60;
  const quickaction::Outcome outcome =
      quickaction::resolve(action, trigger, activityManager.isForegroundReaderActivity(), gpio.hasTouch());
  switch (outcome) {
    case quickaction::Outcome::Refresh:
      LOG_DBG("MAIN", "Manual screen refresh triggered");
      if (!activityManager.handleForcedRefresh()) {
        RenderLock lock;
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
      break;
    case quickaction::Outcome::Sleep:
      enterDeepSleep();
      break;
    case quickaction::Outcome::PageForward:
      activityManager.pageTurn(true);
      break;
    case quickaction::Outcome::ReaderMenu:
    case quickaction::Outcome::SaveQuote:
      activityManager.readerShortcut(outcome == quickaction::Outcome::ReaderMenu ? ReaderShortcut::Menu
                                                                                 : ReaderShortcut::Quote);
      break;
    case quickaction::Outcome::SideForward:
#ifndef SIMULATOR  // no motion sensor there, so no double tap reaches this
      gpio.injectPresses(sideButton(true), QUICK_PRESS_HOLD_MS, 1, 0);
#endif
      break;
    case quickaction::Outcome::Back:
    case quickaction::Outcome::Confirm:
#ifndef SIMULATOR  // no motion sensor there, so no shake reaches this
      // One short press of the key through the real input path, as if pressed.
      gpio.injectPresses(outcome == quickaction::Outcome::Back ? mappedInputManager.physicalBack() : SETTINGS.frontButtonConfirm,
                         QUICK_PRESS_HOLD_MS, 1, 0);
#endif
      break;
    case quickaction::Outcome::None:
      break;
  }
}

#if defined(TENOR_PRESS_PROBE) && !defined(SIMULATOR)
static HalFile tapRecFile;
static void tapRecFrames(const Imu::RawFrame* const frames, const bool* const gaps, const uint16_t count) {
  if (!tapRecFile.isOpen()) return;
  uint8_t record[13];
  for (uint16_t i = 0; i < count; ++i) {
    record[0] = gaps[i] ? 1 : 0;
    memcpy(&record[1], &frames[i], 12);
    tapRecFile.write(record, sizeof(record));
  }
}
static void tapRecCommand(const String& cmd) {
  if (cmd == "TAP_REC_START") {
    Storage.mkdir("/v1019");
    const bool ok = Storage.openFileForWrite("TREC", "/v1019/fifo.bin", tapRecFile);
    halTiltSensor.probeFrameSink(ok ? tapRecFrames : nullptr);
    logSerial.printf("TAP_REC:start,%d,t=%lu\n", ok, millis());
  } else if (cmd == "TAP_REC_STOP") {
    halTiltSensor.probeFrameSink(nullptr);
    const size_t size = tapRecFile.isOpen() ? tapRecFile.size() : 0;
    tapRecFile.close();
    logSerial.printf("TAP_REC:stop,bytes=%u,t=%lu\n", static_cast<unsigned>(size), millis());
  } else if (tapRecFile.isOpen()) {
    uint8_t record[13] = {2};
    const String label = cmd.substring(9);
    memcpy(&record[1], label.c_str(), std::min<size_t>(12, label.length()));
    tapRecFile.write(record, sizeof(record));
    tapRecFile.flush();
    logSerial.printf("TAP_MARK:%s,bytes=%u,t=%lu\n", label.c_str(), static_cast<unsigned>(tapRecFile.size()), millis());
  }
}

// CMD:CAT <path>: a file from the card over the cable, "CAT_START:<bytes>", the bytes as they are,
// then "CAT_END:<crc32>" (zlib's). The 1 ms send timeout is raised for the transfer
// only, so a full USB buffer waits for the host instead of dropping bytes.
static void catCommand(const String& path) {
  [[maybe_unused]] LogSerialBinary binary;
  HalFile file;
  if (!Storage.openFileForRead("CAT", path.c_str(), file)) {
    logSerial.printf("CAT_FAIL:%s\n", path.c_str());
    return;
  }
  static uint8_t buf[1024];
  uint32_t crc = 0xFFFFFFFFu;
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(500);
#endif
  logSerial.printf("CAT_START:%u\n", static_cast<unsigned>(file.size()));
  int n;
  bool sending = true;
  while (sending && (n = file.read(buf, sizeof(buf))) > 0) {
    for (int i = 0; i < n; ++i) {
      crc ^= buf[i];
      for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    for (int sent = 0; sent < n;) {
      const size_t w = logSerial.write(buf + sent, n - sent);
      if (w == 0) {
        sending = false;  // The host stopped reading: the CRC tells it the file is short.
        break;
      }
      sent += static_cast<int>(w);
    }
  }
  file.close();
  logSerial.printf("\nCAT_END:%08lx\n", static_cast<unsigned long>(~crc));
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);
#endif
}

// CMD:PUT <path> <size> <crc32 as 8 hex digits>: the file comes over the cable and is written to the card.
// "PUT:READY <size>" says the card is open; the host then sends <size> raw bytes, at most 4096 before it
// reads "PUT:ACK <bytes so far>". The bytes go to <path>.tmp in 1024 byte pieces (nothing is allocated by
// size), the sum is checked, and a match renames the file over <path>: "PUT:OK <size> <crc>". Otherwise
// the .tmp is removed and the line is "PUT:FAIL <reason>". 10 s without a byte is a failure.
static void putCommand(const String& args) {
  putfile::Command cmd;
  if (!putfile::parse(args.c_str(), cmd)) {
    logSerial.printf("PUT:FAIL bad command\n");
    return;
  }
  const std::string tmpPath = cmd.path + ".tmp";
  HalFile file;
  if (!Storage.openFileForWrite("PUT", tmpPath.c_str(), file)) {
    logSerial.printf("PUT:FAIL cannot open %s\n", tmpPath.c_str());
    return;
  }
  struct Port {
    size_t sinceAck = 0, total = 0;
    size_t read(uint8_t* buf, size_t max) {
      const int waiting = logSerial.available();
      if (waiting <= 0) {
        delay(1);
        return 0;
      }
      return logSerial.readBytes(buf, std::min<size_t>(max, static_cast<size_t>(waiting)));
    }
  } port;
  struct Sink {
    HalFile& file;
    Port& port;
    bool write(const uint8_t* buf, size_t n) {
      if (file.write(buf, n) != n) return false;
      port.total += n;
      port.sinceAck += n;
      if (port.sinceAck >= 4096) {
        port.sinceAck = 0;
        logSerial.printf("PUT:ACK %u\n", static_cast<unsigned>(port.total));
      }
      return true;
    }
  } sink{file, port};
  struct Clock {
    uint32_t now() { return millis(); }
  } clock;
  static uint8_t buf[1024];
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(500);
#endif
  logSerial.printf("PUT:READY %u\n", static_cast<unsigned>(cmd.size));
  uint32_t crc = 0;
  const putfile::Result result = putfile::receive(cmd, port, sink, clock, buf, sizeof(buf), 10000, &crc);
  file.close();
  if (result == putfile::Result::Ok) {
    Storage.remove(cmd.path.c_str());
    if (Storage.rename(tmpPath.c_str(), cmd.path.c_str())) {
      logSerial.printf("PUT:OK %u %08lx\n", static_cast<unsigned>(cmd.size), static_cast<unsigned long>(crc));
    } else {
      Storage.remove(tmpPath.c_str());
      logSerial.printf("PUT:FAIL rename\n");
    }
  } else {
    Storage.remove(tmpPath.c_str());
    logSerial.printf("PUT:FAIL %s\n", putfile::reason(result));
  }
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);
#endif
}

// CMD:CUR_LOG <s>: for the next <s> seconds, one fuel gauge line every 10 s (average and
// instant current in mA, voltage, whether the motion sensor samples, the shake action),
// kept in RAM too, so a run with the cable out is read back with CMD:CUR_LOG and no number.
struct CurLogRow {
  uint32_t ms;
  int16_t avgMa, curMa;
  uint16_t mv;
  uint8_t imuAwake, shakeAction;
};
static CurLogRow curLogRows[96];
static uint8_t curLogCount = 0;
static unsigned long curLogUntilMs = 0, curLogNextMs = 0;

static void printCurLogRow(const CurLogRow& r) {
  logSerial.printf("CUR:t=%lu,avg=%d,cur=%d,mv=%u,imu=%u,shake=%u\n", static_cast<unsigned long>(r.ms), r.avgMa,
                   r.curMa, r.mv, r.imuAwake, r.shakeAction);
}

static void curLogTick() {
  if (curLogUntilMs == 0 || static_cast<long>(millis() - curLogNextMs) < 0) return;
  if (static_cast<long>(millis() - curLogUntilMs) > 0) {
    curLogUntilMs = 0;
    return;
  }
  curLogNextMs = millis() + 10000;
  const uint8_t addr = BoardConfig::ACTIVE.batteryGauge.gaugeAddr;
  const auto reg16 = [addr](const uint8_t reg) -> int {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (addr == 0 || Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, uint8_t{2}, uint8_t{1}) < 2) return -1;
    const int lo = Wire.read();
    return lo | (Wire.read() << 8);
  };
  const CurLogRow row{static_cast<uint32_t>(millis()), static_cast<int16_t>(reg16(0x14)),
                      static_cast<int16_t>(reg16(0x0C)), static_cast<uint16_t>(reg16(0x08)),
                      static_cast<uint8_t>(halTiltSensor.isAwake()), SETTINGS.shakeAction};
  if (curLogCount < sizeof(curLogRows) / sizeof(curLogRows[0])) curLogRows[curLogCount++] = row;
  printCurLogRow(row);
}
#endif

#if defined(TENOR_GAUGE_LOG) && !defined(SIMULATOR)
// Measurement build only: one fuel gauge line a minute while awake (and one at every boot), kept in
// RAM and rewritten whole to /v1016/pin/<boot>-<part>.csv, so a battery run can be read off the card.
static void gaugeLogTick() {
  static unsigned long last = 0;
  static std::string lines;
  static unsigned part = 0;
  static long boot = 0;
  if (last != 0 && millis() - last < 60000) return;
  last = millis();
  if (boot == 0) boot = static_cast<long>(time(nullptr));
  const uint8_t addr = BoardConfig::ACTIVE.batteryGauge.gaugeAddr;
  const auto reg16 = [addr](const uint8_t reg) -> int {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (addr == 0 || Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, uint8_t{2}, uint8_t{1}) < 2) return -1;
    const int lo = Wire.read();
    return lo | (Wire.read() << 8);
  };
  char row[128];
  snprintf(row, sizeof(row), "%ld,%lu,%d,%d,%d,%d,%d,%d,0x%04x,%s\n", static_cast<long>(time(nullptr)), millis(),
           reg16(0x08), static_cast<int16_t>(reg16(0x0C)), static_cast<int16_t>(reg16(0x14)), reg16(0x10),
           reg16(0x12), reg16(0x2C), reg16(0x0A), activityManager.isReaderActivity() ? "doc" : "khac");
  lines += row;
  Storage.mkdir("/v1016");
  Storage.mkdir("/v1016/pin");
  char path[64];
  snprintf(path, sizeof(path), "/v1016/pin/%ld-%u.csv", boot, part);
  HalFile file;
  if (Storage.openFileForWrite("GLOG", path, file)) {
    file.print("epoch,ms,mv,cur,avg,rm,fcc,soc,flags,man\n");
    file.write(reinterpret_cast<const uint8_t*>(lines.data()), lines.size());
    file.close();
  }
  if (lines.size() > 6000) {
    lines.clear();
    part++;
  }
}
#endif

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

#ifdef TENOR_PRESS_PROBE
  probeReprintDryRunLine();
#endif
  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
  mappedInputManager.update();
#ifndef SIMULATOR
  // The render task draws the gauge reading polled here (at most every 1.5 s).
  powerManager.pollGauge();
  // The battery's capacity into the gauge, once the first screen is up; never blocks.
  if (activityManager.hasDrawnFrame()) halGaugeCapacity.tick();
#endif
#if defined(TENOR_TAP_LOG) && !defined(SIMULATOR)
  tapLogTick();
#endif
#if defined(TENOR_GAUGE_LOG) && !defined(SIMULATOR)
  gaugeLogTick();
#endif
#if defined(TENOR_PRESS_PROBE) && !defined(SIMULATOR)
  curLogTick();
#endif

#if CROSSPOINT_BLE_HID_HOST
  // Page turner BLE (lib/BlePageTurner): one pass before USB's early return, so a card handed
  // over to USB or a file transfer stops the radio before anything else. Activity onEnter()
  // can already have done blocking storage or Wi-Fi work before this loop resumes.
  bool bleInputActivity = false;
  {
    bleturner::Scene scene{};
    scene.where =
        activityManager.isForegroundReaderActivity() ? bleturner::Where::Reader : bleturner::Where::Elsewhere;
    scene.visit = activityManager.activityGeneration();
    scene.pageShown = activityManager.isForegroundReaderReady();
    // A book still building its index in the background keeps the radio off until the index
    // is whole (EpubReaderActivity::holdsRadio); the device keys work meanwhile.
    scene.bookIndexing = activityManager.foregroundReaderHoldsRadio();
    scene.storageBusy = activityManager.requiresExclusiveStorageLoop() || filetransfer::isActive();
    scene.wifiOn = WiFi.getMode() != WIFI_MODE_NULL;
    scene.sleeping = activityManager.isSleepTransition();
    // A page-key release or touch in the book grants one fresh start after the idle stop.
    scene.localKey = mappedInputManager.wasReleased(MappedInputManager::Button::PageBack) ||
                     mappedInputManager.wasReleased(MappedInputManager::Button::PageForward) ||
                     mappedInputManager.wasReleased(MappedInputManager::Button::Left) ||
                     mappedInputManager.wasReleased(MappedInputManager::Button::Right) || gpio.wasTouchActivity();
    bleInputActivity = bleturner::tick(scene);
  }
#endif

  if (activityManager.requiresExclusiveStorageLoop()) {
    // USB Drive handed the raw SD card to the host. Do not run screenshots,
    // sleep, shortcuts, or normal navigation while its filesystem is detached.
    activityManager.loop();
    if (activityManager.preventAutoSleep()) {
      powerManager.setPowerSaving(false);
      delay(10);
    } else {
      // No host is active, so a slower loop is safe. The activity itself times
      // out the raw-storage handoff rather than entering deep sleep detached.
      powerManager.setPowerSaving(true);
      delay(50);
    }
    return;
  }

  // Tab gestures are sampled by the activity at the end of the previous loop.
  // Preserve their activity before the foreground route clears the flag.
  const bool pendingTiltActivity = halTiltSensor.hadActivity();
  halTiltSensor.noteScreen(activityManager.activityGeneration());
  const bool foregroundReader = activityManager.isForegroundReaderActivity();
  const bool foregroundActivityManagesTiltSensor = activityManager.isForegroundActivityManagingTiltSensor();
  updateTiltSensorForForegroundActivity(foregroundReader, foregroundActivityManagesTiltSensor);

  renderer.setFadingFix(SETTINGS.fadingFix);

  // The ROM console does not depend on Arduino USB CDC's connection state.
  if ((Serial || FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF) && millis() - lastMemPrint >= 10000) {
    const auto heap = HalMemory::getInternalHeap();
    LOG_INF("MEM", "Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes", heap.freeBytes,
            heap.totalBytes, heap.minFreeBytes, heap.largestBlockBytes);
#ifdef BOARD_HAS_PSRAM
    const auto psram = HalMemory::getPsramHeap();
    LOG_INF("MEM", "PSRAM: Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes",
            psram.freeBytes, psram.totalBytes, psram.minFreeBytes, psram.largestBlockBytes);
#endif
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  // In the X4 Pro probe build a line can also come from the card's script (platform/ColdLog.h).
  String line;
  if (logSerial.available() > 0) line = logSerial.readStringUntil('\n');
#if TENOR_COLD_LOG
  else
    line = coldlog::nextLine(activityManager.hasDrawnFrame());
#endif
  if (line.length() > 0) {
    if (line.startsWith("CMD:")) {
      powerManager.setPowerSaving(false);
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        [[maybe_unused]] LogSerialBinary binary;
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
#ifdef FREEINK_TLS_AUDIT
      } else if (cmd == "TLS_AUDIT_JOIN") {
        activityManager.pushActivity(makeUniqueNoThrow<TlsAuditActivity>(renderer, mappedInputManager));
      } else if (cmd == "TLS_AUDIT_NO_CA") {
        const uint32_t before = ESP.getFreeHeap();
        unsigned accepted = 0;
        for (int i = 0; i < 32; ++i) {
          freeink::SecureClient client;
          accepted += client.connect("self-signed.badssl.com", 443) != 0;
        }
        logSerial.printf("TLS_AUDIT_NO_CA:accepted=%u,before=%u,after=%u\n", accepted, before, ESP.getFreeHeap());
      } else if (cmd == "TLS_AUDIT_FONT_DOWNLOAD") {
        auto activity = makeUniqueNoThrow<FontDownloadActivity>(renderer, mappedInputManager);
        if (activity) {
          activity->setAuditDownload();
          activityManager.pushActivity(std::move(activity));
        }
      } else if (cmd == "TLS_AUDIT_FONTS") {
        activityManager.pushActivity(makeUniqueNoThrow<FontDownloadActivity>(renderer, mappedInputManager));
      } else if (cmd.startsWith("TLS_AUDIT ")) {
        const String name = cmd.substring(10);
        const char* url = nullptr;
        if (name == "github" || name == "expired-clock")
          url = "https://github.com/robots.txt";
        else if (name == "cdn")
          url = "https://release-assets.githubusercontent.com/";
        else if (name == "cross")
          url = "https://cross.tenor.vn/";
        else if (name == "kosync")
          url = "https://sync.koreader.rocks/";
        else if (name == "valid")
          url = "https://sha256.badssl.com/";
        else if (name == "selfsigned")
          url = "https://self-signed.badssl.com/";
        else if (name == "hostname")
          url = "https://wrong.host.badssl.com/";
        else if (name == "expired")
          url = "https://expired.badssl.com/";
        if (url && (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
                    ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC)) {
          logSerial.printf("TLS_AUDIT:LOW_MEMORY,heap=%u,largest=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        } else if (url) {
          const time_t clockBefore = time(nullptr);
          const uint32_t probeStart = millis();
          if (name == "expired-clock") {
            timeval future{clockBefore + 2 * 365 * 86400, 0};
            settimeofday(&future, nullptr);
          }
          size_t bytes = 0;
          logSerial.printf("TLS_AUDIT_START:%s,heap=%u,largest=%u\n", name.c_str(), ESP.getFreeHeap(),
                           ESP.getMaxAllocHeap());
          const bool ok = HttpDownloader::fetchUrl(
              url,
              [&bytes](const uint8_t*, size_t len) {
                bytes += len;
                return bytes <= 65536;
              },
              "", "", nullptr, false);
          if (name == "expired-clock") {
            timeval restored{clockBefore + static_cast<time_t>((millis() - probeStart) / 1000), 0};
            settimeofday(&restored, nullptr);
          }
          logSerial.printf("TLS_AUDIT_END:%s,ok=%d,bytes=%u,heap=%u,largest=%u\n", name.c_str(), ok, (unsigned)bytes,
                           ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        } else {
          logSerial.printf("TLS_AUDIT:INVALID_FIXTURE\n");
        }
#endif
#ifdef TENOR_OTA_ACCEPTANCE
      } else if (cmd == "OTA_ACCEPTANCE") {
        activityManager.pushActivity(makeUniqueNoThrow<OtaUpdateActivity>(renderer, mappedInputManager));
#endif
#ifdef TENOR_PRESS_PROBE
      } else if (cmd.startsWith("PRESS ")) {
        // CMD:PRESS <NEXT|PREV|SIDE_NEXT|SIDE_PREV|POWER|BACK|CONFIRM> <holdMs> <count> <gapMs>: plays
        // presses through the button hook while this loop keeps running, so they can land
        // mid-render. Names follow the portrait reader mapping of the current settings.
        char name[12] = {};
        unsigned hold = 0, count = 0, gap = 0;
        int button = -1;
        if (SCAN_COMMAND(cmd.c_str() + 6, "%11s %u %u %u", name, &hold, &count, &gap) == 4) {
          const String n(name);
          if (n == "NEXT") button = SETTINGS.frontButtonRight;
          if (n == "PREV") button = SETTINGS.frontButtonLeft;
          if (n == "SIDE_NEXT") button = sideButton(true);
          if (n == "SIDE_PREV") button = sideButton(false);
          if (n == "POWER") button = HalGPIO::BTN_POWER;
          if (n == "BACK") button = mappedInputManager.physicalBack();
          if (n == "CONFIRM") button = SETTINGS.frontButtonConfirm;
        }
        if (button >= 0) gpio.injectPresses(button, hold, count, gap);
        logSerial.printf("PRESS:button=%d,hold=%u,count=%u,gap=%u,t=%lu\n", button, hold, count, gap, millis());
#if FREEINK_CAP_TOUCH && !defined(SIMULATOR)
      } else if (cmd.startsWith("TAP ") || cmd.startsWith("SWIPE ")) {
        // CMD:TAP <x> <y> [holdMs] and CMD:SWIPE <x0> <y0> <x1> <y1> <ms>, in the coordinates the app
        // draws in (portrait 480 x 800): one finger down, held or moved over the given time, then lifted.
        const bool tap = cmd.startsWith("TAP ");
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        unsigned ms = 80;
        bool ok = false;
        if (tap) {
          ok = SCAN_COMMAND(cmd.c_str() + 4, "%d %d %u", &x0, &y0, &ms) >= 2;
          x1 = x0;
          y1 = y0;
        } else {
          ok = SCAN_COMMAND(cmd.c_str() + 6, "%d %d %d %d %u", &x0, &y0, &x1, &y1, &ms) == 5;
        }
        if (ok) {
          probeTouchPlan = touchprobe::swipe(x0, y0, x1, y1, ms);
          InputManager::setTouchProbeHook(probeTouchHook);
        }
        logSerial.printf("TOUCH:ok=%d,from=%d,%d,to=%d,%d,ms=%u,t=%lu\n", ok, x0, y0, x1, y1, ms, millis());
#endif
#endif
#if defined(TENOR_UI_ACCEPTANCE) || defined(TENOR_PRESS_PROBE)
      } else if (cmd == "UI_READER_NEXT" || cmd == "UI_READER_PREV") {
        const bool queued = activityManager.pageTurn(cmd == "UI_READER_NEXT");
        logSerial.printf("UI_READER:synthetic_external=1,queued=%d,generation=%u\n", queued,
                         static_cast<unsigned>(activityManager.activityGeneration()));
#if CROSSPOINT_BLE_HID_HOST
      } else if (cmd == "BLE_TEST_BEGIN") {
        // In-memory only. A reset restores the saved user preference.
        SETTINGS.ble.enabled = 1;
        const bool ok = bleturner::switchOn();
        logSerial.printf("BLE_TEST:begin=%d,heap=%u,largest=%u\n", ok, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      } else if (cmd == "BLE_TEST_END") {
        SETTINGS.ble.enabled = 0;
        const bool ended = bleturner::switchOff();
        logSerial.printf("BLE_TEST:end=%d,heap=%u,largest=%u\n", ended, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      } else if (cmd == "BLE_TEST_SCAN") {
        if (bleturner::status().running) bleturner::scan(5000);
      } else if (cmd == "BLE_TEST_STATUS") {
        const auto ble = bleturner::status();
        logSerial.printf("BLE_TEST:enabled=%u,running=%d,scanning=%d,connected=%d,busy=%d,initializing=%d,heap=%u,largest=%u\n",
                         SETTINGS.ble.enabled, ble.running, ble.scanning, ble.connected,
                         ble.starting || ble.running || ble.stopping, ble.starting, ESP.getFreeHeap(),
                         ESP.getMaxAllocHeap());
#endif
#ifdef TENOR_UI_ACCEPTANCE
      } else if (cmd == "FILE_TRANSFER_AUTOCONNECT") {
        auto activity = makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, mappedInputManager);
        if (activity) {
          activity->requestAutoJoinForTest();
          activityManager.replaceActivity(std::move(activity));
          logSerial.printf("UI_TRANSFER:STARTING\n");
        } else {
          logSerial.printf("UI_TRANSFER:LOW_MEMORY\n");
        }
      } else if (cmd == "HOME_NEXT" || cmd == "HOME_PREV") {
        activityManager.stepHomeForTest(cmd == "HOME_NEXT" ? 1 : -1);
      } else if (cmd.startsWith("HOME_TAB ")) {
        activityManager.tabHomeForTest(cmd.substring(9).toInt());
#endif
      } else if (cmd.startsWith("SET ")) {
        // CMD:SET <ten-json>=<so>: dat mot cai dat so nguyen bang dung ten JSON
        // cua no (SettingsList.h), luu xuong the va tra ve gia tri da ghi. Chi
        // ton tai trong ban nghiem thu qua USB, khong nam trong ban phat hanh.
        const String kv = cmd.substring(4);
        const int bang = kv.indexOf('=');
        bool dat = false;
        if (bang > 0) {
          const String ten = kv.substring(0, bang);
          const long so = kv.substring(bang + 1).toInt();
          dat = visitDiagnosticSetting(ten, [so](const SettingInfo& info) {
            if (info.valuePtr != nullptr) {
              SETTINGS.*(info.valuePtr) = static_cast<uint8_t>(so);
              return true;
            }
            if (info.valueSetter) {
              info.valueSetter(static_cast<uint8_t>(so));
              return true;
            }
            return false;
          });
        }
        if (dat) {
          SETTINGS.saveToFile();
          // The clock zone is pushed into HalClock by the screens that change it; a SET has to do
          // the same. Note that clockAutoTimezone=1 (the default) overrides clockTimezone.
          if (kv.startsWith("clockTimezone=") || kv.startsWith("clockDst=") || kv.startsWith("clockAutoTimezone="))
            timezones::applyToClock();
          logSerial.printf("SET:%s\n", kv.c_str());
        } else {
          logSerial.printf("SET:INVALID:%s\n", kv.c_str());
        }
      } else if (cmd.startsWith("GET ")) {
        // CMD:GET <ten-json>: doc lai dung gia tri dang giu trong RAM.
        const String ten = cmd.substring(4);
        const bool found = visitDiagnosticSetting(ten, [](const SettingInfo& info) {
          if (info.valuePtr != nullptr) {
            logSerial.printf("GET:%s=%u\n", info.key, SETTINGS.*(info.valuePtr));
            return true;
          }
          if (info.valueGetter) {
            logSerial.printf("GET:%s=%u\n", info.key, info.valueGetter());
            return true;
          }
          return false;
        });
        if (!found) logSerial.printf("GET:INVALID:%s\n", ten.c_str());
      } else if (cmd.startsWith("FONT_TEST ")) {
        char family[32] = {};
        unsigned point = 0, weight = 0;
        if (SCAN_COMMAND(cmd.c_str() + 10, "%31s %u %u", family, &point, &weight) == 3 && point >= 12 && point <= 26 &&
            weight < readerInk::LEVEL_COUNT && sdFontSystem.registry().findFamily(family)) {
          const uint32_t started = millis();
          {
            RenderLock lock;
            snprintf(SETTINGS.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName), "%s", family);
            SETTINGS.fontPointSize = point;
            SETTINGS.readerInkWeight = weight;
            sdFontSystem.ensureLoaded(renderer);
          }
          logSerial.printf("FONT_TEST:family=%s,pt=%u,requested=%u,effective=%u,ms=%u,heap=%u,largest=%u\n", family,
                           point, weight, sdFontSystem.effectiveWeight(), millis() - started, ESP.getFreeHeap(),
                           ESP.getMaxAllocHeap());
        } else {
          logSerial.printf("FONT_TEST:INVALID\n");
        }
      } else if (cmd == "LANGUAGE_ZH") {
        // In-memory only: a reset restores the user's saved language.
        I18N.setLanguage(Language::ZH_HANS);
        activityManager.goHome();
        logSerial.printf("UI_LANGUAGE:ZH_HANS\n");
      } else if (cmd == "LANGUAGE_RESTORE") {
        I18N.setLanguage(static_cast<Language>(SETTINGS.language));
        activityManager.goHome();
        logSerial.printf("UI_LANGUAGE:RESTORED\n");
#ifdef TENOR_PRESS_PROBE
      } else if (cmd.startsWith("BLE_RAW")) {
        // CMD:BLE_RAW <hex bytes>: one HID frame through the real ingest path, as if the
        // remote had sent it; the next loop pass routes it. With no remote connected the
        // route takes the built-in three-button table, so the hot path from frame to page
        // or chapter can be timed with no hand on a remote. Open a book first. With no
        // bytes it only reports `overflow`, the raw presses dropped since boot. Frames
        // split by ',' go in back to back in this one pass, before the loop drains any:
        // a burst of taps queued while the reader is busy.
        uint8_t frame[16];
        const char* p = cmd.c_str() + 7;
        bleturner::injectFrame(nullptr, 0);
        const unsigned long t0 = micros();
        size_t n = 0;
        unsigned frames = 0;
        for (;;) {
          size_t len = 0;
          unsigned v = 0;
          int used = 0;
          while (len < sizeof(frame) && SCAN_COMMAND(p, " %x%n", &v, &used) == 1) {
            frame[len++] = static_cast<uint8_t>(v);
            p += used;
          }
          if (len > 0) {
            bleturner::injectFrame(frame, len);
            ++frames;
            n = len;
          }
          while (*p == ' ') ++p;
          if (*p != ',') break;
          ++p;
        }
        logSerial.printf("BLE_RAW:len=%u,frames=%u,ingest_us=%lu,running=%d,overflow=%u,t=%lu\n",
                         static_cast<unsigned>(n), frames, micros() - t0, bleturner::status().running,
                         bleturner::rawOverflows(), millis());
#endif
#endif
#ifdef TENOR_PRESS_PROBE
      } else if (cmd.startsWith("OTA_DRYRUN ")) {
        // CMD:OTA_DRYRUN <manifest-url> [runs]: restarts into the update boot like a confirmed update,
        // joins the saved Wi-Fi, then runs the real check and download from a manifest under
        // cross.tenor.vn/firmware/ into the spare slot, verified and closed like an install, whatever
        // the version, never switching the boot slot. One run a boot and one OTA_DRYRUN_RESULT line
        // a run (1-20 runs); after the last run the device restarts to Home. Send it from Home.
        String arg = cmd.substring(11);
        arg.trim();
        const int space = arg.indexOf(' ');
        const String url = space < 0 ? arg : arg.substring(0, space);
        const int runs = space < 0 ? 1 : std::max(1L, std::min(20L, arg.substring(space + 1).toInt()));
        if (url.length() >= sizeof(probeDryRunUrl)) {
          logSerial.printf("OTA_DRYRUN_RESULT ok=0 step=url err=HTTP_ERROR run=0/%d\n", runs);
        } else {
          // The path of a confirmed update: restart into the update boot, one run a boot.
          std::memcpy(probeDryRunUrl, url.c_str(), url.length() + 1);
          logSerial.printf("OTA_DRYRUN_START runs=%d heap=%u largest=%u\n", runs, ESP.getFreeHeap(),
                           ESP.getMaxAllocHeap());
          delay(3000);  // the serial reader takes this line before the port drops for the restart
          activityManager.closeForRestart();
          silentRestartToOta(static_cast<uint8_t>(runs), static_cast<uint8_t>(runs));
          activityManager.goHome();  // only when a sleep superseded the restart
        }
      } else if (cmd.startsWith("WAKE_TIMER ")) {
        probeWakeSeconds = static_cast<uint32_t>(cmd.substring(11).toInt());
        probeWakeMagic = PROBE_WAKE_MAGIC;
        logSerial.printf("WAKE_TIMER:%u\n", static_cast<unsigned>(probeWakeSeconds));
      } else if (cmd.startsWith("WAKE_HOLD ")) {
        probeWakeHoldMs = static_cast<uint32_t>(cmd.substring(10).toInt());
        probeWakeHoldMagic = PROBE_WAKE_MAGIC;
        logSerial.printf("WAKE_HOLD:%u\n", static_cast<unsigned>(probeWakeHoldMs));
      } else if (cmd.startsWith("STARVE_BUILD ")) {
        // CMD:STARVE_BUILD <n>: the n-th heap check of a chapter build step fails once (once, then
        // the count is spent). Sends the build down the starved path: park, shed the font caches,
        // resume. Send it before the press that builds.
        buildprobe::starveAfterSteps = static_cast<uint32_t>(std::max(0L, cmd.substring(13).toInt()));
        logSerial.printf("STARVE_BUILD:%u\n", static_cast<unsigned>(buildprobe::starveAfterSteps));
      } else if (cmd.startsWith("KEEP_HEAP ")) {
        probeKeepHeap = cmd.substring(10).toInt() != 0;
        logSerial.printf("KEEP_HEAP:%d\n", probeKeepHeap ? 1 : 0);
      } else if (cmd == "LOGDUMP") {
        probeLogDump();
#if LOG_SERIAL_HAS_TX_TIMEOUT
      } else if (cmd.startsWith("TXTIMEOUT ")) {
        // CMD:TXTIMEOUT <ms>: the serial write timeout until the next restart, for the long dumps
        // (LOGDUMP, CAT, SCREENSHOT) over a CDC port that drops bytes at the load-bearing 1 ms.
        const long ms = std::max(1L, std::min(5000L, cmd.substring(10).toInt()));
        logSerial.setTxTimeoutMs(static_cast<uint32_t>(ms));
        logSerial.printf("TXTIMEOUT:%ld\n", ms);
#endif
#if FREEINK_DEVICE_X4PRO && !defined(SIMULATOR)
      } else if (fwprobe::command(cmd)) {
        // EFUSE, OTA_STATE, FLASH_DUMP, SD_FLASH, ROLLBACK_TEST: handled there
      } else if (cmd.startsWith("SHOT ")) {
        // CMD:SHOT <path>: the framebuffer to the card as the screenshot keys save it, a 1-bit BMP
        // turned to the panel's portrait orientation.
        String path = cmd.substring(5);
        path.trim();
        RenderLock lock;
        const bool ok = ScreenshotUtil::saveFramebufferAsBmp(path.c_str(), renderer.getFrameBuffer(),
                                                             renderer.getDisplayWidth(), renderer.getDisplayHeight());
        logSerial.printf("SHOT:ok=%d,path=%s\n", ok, path.c_str());
      } else if (cmd == "USB_DRIVE") {
        // CMD:USB_DRIVE: USB Drive, opened as the menu opens it. The log goes to the card first, the
        // host owns the card from here; its eject restarts the unit, which then runs the next script.
        logSerial.printf("USB_DRIVE:t=%lu\n", millis());
        coldlog::flush("usb-drive");
        activityManager.goToUsbDrive();
#endif
#if FREEINK_DEVICE_X4PRO
      } else if (cmd.startsWith("STROKE_LOG ")) {
        // CMD:STROKE_LOG 1 <label> / STROKE_LOG 0: every stroke drawn on the glass and what the
        // scribble recognizer made of it (STROKE, SCRIBBLE lines), the label naming what the person
        // was asked to draw (strike, circle, swipe, tap). test/scribble/log_to_samples.py turns the
        // lines into test samples.
        const String arg = cmd.substring(11);
        const int space = arg.indexOf(' ');
        const String label = space > 0 ? arg.substring(space + 1) : String("-");
        mappedInputManager.setStrokeLog(arg.toInt() != 0, label.c_str());
        logSerial.printf("STROKE_LOG:%d,%s\n", arg.toInt() != 0 ? 1 : 0, label.c_str());
#endif
      } else if (cmd == "PANIC") {
#if TENOR_COLD_LOG
        coldlog::flush("panic");
#endif
        abort();  // a crash reboot, for the paths that follow one
      } else if (cmd.startsWith("I2C_RACE ")) {
        // Two tasks on the gauge at once, as the render task and the loop did: a second task
        // reads state of charge while this loop reads the current register. Any reading far
        // from the first one is a reply taken from the other task's transaction.
        static volatile bool raceRun;
        static volatile uint32_t raceReads, raceOdd, raceMin, raceMax, raceFirst;
        raceRun = true;
        raceReads = raceOdd = 0;
        raceMin = 0xffff;
        raceMax = 0;
        raceFirst = 0xffff;
        xTaskCreate(
            [](void*) {
              const BatteryMonitor gauge;
              while (raceRun) {
                uint16_t soc = 0;
                if (!gauge.readPercentageChecked(soc)) continue;
                ++raceReads;
                if (raceFirst == 0xffff) raceFirst = soc;
                if (soc + 1 < raceFirst || soc > raceFirst + 1) {
                  ++raceOdd;
                  if (soc < raceMin) raceMin = soc;
                  if (soc > raceMax) raceMax = soc;
                }
              }
              raceRun = true;
              vTaskDelete(nullptr);
            },
            "i2c-race", 3072, nullptr, 1, nullptr);
        const unsigned long end = millis() + static_cast<unsigned long>(cmd.substring(9).toInt());
        uint32_t usbReads = 0;
        while (static_cast<long>(millis() - end) < 0) {
          gpio.isUsbConnected();
          ++usbReads;
        }
        raceRun = false;
        while (!raceRun) delay(5);
        logSerial.printf("I2C_RACE:soc_reads=%u,odd=%u,first=%u,odd_min=%u,odd_max=%u,usb_reads=%u\n",
                         static_cast<unsigned>(raceReads), static_cast<unsigned>(raceOdd),
                         static_cast<unsigned>(raceFirst), static_cast<unsigned>(raceMin),
                         static_cast<unsigned>(raceMax), static_cast<unsigned>(usbReads));
      } else if (cmd == "STATS_DUMP") {
        // What decides whether reading is recorded, read back after a battery wake: the
        // clock that dates it, whether the store loaded, and whether a save lands now.
        // The lock keeps the save off the SD bus while a page paints, as the reader's own saves are.
        RenderLock lock;
        const auto& st = READING_STATS;
        const uint32_t day = ReadingStatsStore::currentDay();
        uint32_t todayMs = 0, todayTurns = 0;
        for (const auto& d : st.kho.cacNgay())
          if (d.ma == day) todayMs = d.phut * 60000u + d.leMs, todayTurns = d.trang;
        const std::string main = ReadingStatsStore::getFilePath();
        const bool saved = READING_STATS.saveToFile();
        logSerial.printf(
            "STATS:readable=%d,save=%d,clock=%d,day=%lu,today_ms=%lu,today_turns=%lu,undated_min=%lu,undated_ms=%u,"
            "undated_turns=%lu,book_ms=%lu,book_turns=%lu,main=%d,bak=%d,tmp=%d,reset=%d,dir=%d,reader=%d,"
            "heap=%u,largest=%u,t=%lu\n",
            st.statisticsReadable, saved, halClock.hasValidTime(), static_cast<unsigned long>(day),
            static_cast<unsigned long>(todayMs), static_cast<unsigned long>(todayTurns),
            static_cast<unsigned long>(st.kho.phutChuaBietNgay()), st.kho.msChuaBietNgay(),
            static_cast<unsigned long>(st.kho.trangChuaBietNgay()),
            static_cast<unsigned long>(st.activeBook.minutes * 60000u + st.activeBook.remainderMs),
            static_cast<unsigned long>(st.activeBook.turns), Storage.exists(main.c_str()),
            Storage.exists((main + ".bak").c_str()), Storage.exists((main + ".tmp").c_str()),
            Storage.exists("/.crosspoint/reading-stats.reset"), Storage.exists("/.crosspoint/reading-stats"),
            activityManager.isReaderActivity(), ESP.getFreeHeap(), ESP.getMaxAllocHeap(), millis());
      } else if (cmd.startsWith("CLEAR_BOOK_CACHE ")) {
        // Makes a book open like a first open (cover thumbnails and chapters rebuilt).
        const std::string path = cmd.substring(17).c_str();
        const std::string dir = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
        logSerial.printf("CLEAR_BOOK_CACHE:%s,removed=%d\n", dir.c_str(), Storage.removeDir(dir.c_str()));
      } else if (cmd == "BATT") {
        const BatteryMonitor battery;
        const auto st = battery.readStatus();
        logSerial.printf("BATT:soc=%u,mv=%u,charging=%d,shown=%u,t=%lu\n", st.percentage, st.millivolts, st.charging,
                         powerManager.getBatteryPercentage(), millis());
      } else if (cmd.startsWith("IMU_LOG ")) {
        // CMD:IMU_LOG <s> [fast]: raw motion samples for <s> seconds (at most 600). Plain: one
        // "IMU:" line per poll the firmware really makes (50 ms), while every screen and the
        // shake and tilt channels keep running. fast: blocks this loop and reads at 224 Hz.
        const unsigned long seconds = std::min(600L, std::max(0L, cmd.substring(8).toInt()));
        if (cmd.endsWith(" fast")) {
          halTiltSensor.probeFastLog(seconds * 1000UL);
        } else {
          halTiltSensor.probeLogUntil(millis() + seconds * 1000UL);
          logSerial.printf("IMU_LOG:seconds=%lu,shake=%u,strength=%u,tilt=%u,t=%lu\n", seconds, SETTINGS.shakeAction,
                           SETTINGS.shakeStrength, SETTINGS.tiltPageTurn, millis());
        }
      } else if (cmd.startsWith("IMU_MARK ")) {
        // CMD:IMU_MARK <label>: names the part of an IMU_LOG run that follows (do-lac/phan_tich_lac.py).
        logSerial.printf("IMU_MARK:%lu,%s\n", millis(), cmd.substring(9).c_str());
      } else if (cmd == "TAP_REC_START" || cmd == "TAP_REC_STOP" || cmd.startsWith("TAP_MARK ")) {
        // CMD:TAP_REC_START / TAP_MARK <label> / TAP_REC_STOP: every FIFO frame the tap detector
        // gets, to /v1019/fifo.bin on the card (the USB link stalls under a stream of them). 13 bytes
        // a frame: a flag (0, or 1 after lost frames) then ax ay az gx gy gz, int16 little endian raw
        // counts; a mark is a flag 2 then 12 bytes of its label.
#ifndef SIMULATOR
        tapRecCommand(cmd);
#endif
#ifndef SIMULATOR
      } else if (cmd.startsWith("CAT ")) {
        catCommand(cmd.substring(4));
#endif
#ifndef SIMULATOR
      } else if (cmd.startsWith("PUT ")) {
        putCommand(cmd.substring(4));
#endif
      } else if (cmd.startsWith("TAP_LOG ")) {
        // CMD:TAP_LOG <s>: for <s> seconds (at most 600), with double tap on, one "IMU_FIFO:" line
        // per poll (frames read, microseconds the read took, free and lowest heap) and one
        // "IMU_TAP:" line per tap the detector finds (1 single, 2 double).
        const unsigned long seconds = std::min(600L, std::max(0L, cmd.substring(8).toInt()));
        halTiltSensor.probeTapLogUntil(millis() + seconds * 1000UL);
        logSerial.printf("TAP_LOG:seconds=%lu,doubleTap=%u,t=%lu\n", seconds, SETTINGS.doubleTapAction, millis());
#ifndef SIMULATOR
      } else if (cmd.startsWith("CUR_LOG")) {
        const long seconds = cmd.length() > 8 ? cmd.substring(8).toInt() : 0;
        if (seconds > 0) {
          curLogCount = 0;
          curLogNextMs = millis();
          curLogUntilMs = millis() + static_cast<unsigned long>(seconds) * 1000UL;
          logSerial.printf("CUR_LOG:seconds=%ld,shake=%u,t=%lu\n", seconds, SETTINGS.shakeAction, millis());
        } else {
          for (uint8_t i = 0; i < curLogCount; ++i) printCurLogRow(curLogRows[i]);
          logSerial.printf("CUR_LOG_END:rows=%u\n", curLogCount);
        }
#endif
      } else if (cmd == "GAUGE") {
        // CMD:GAUGE: the BQ27220 registers behind the percentage, read once from this loop.
        const uint8_t addr = BoardConfig::ACTIVE.batteryGauge.gaugeAddr;
        const auto reg16 = [addr](const uint8_t reg) -> int {
          Wire.beginTransmission(addr);
          Wire.write(reg);
          if (addr == 0 || Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, uint8_t{2}, uint8_t{1}) < 2)
            return -1;
          const int lo = Wire.read();
          return lo | (Wire.read() << 8);
        };
        // op: OperationStatus(), SEC bits 2:1 read 11 once sealed; cap: the capacity load of this start.
        logSerial.printf(
            "GAUGE:temp=%d,mv=%d,flags=0x%04x,cur=%d,rm=%d,fcc=%d,avg=%d,cyc=%d,soc=%d,soh=%d,dc=%d,op=0x%04x,cap=%s,"
            "t=%lu\n",
            reg16(0x06), reg16(0x08), reg16(0x0A), static_cast<int16_t>(reg16(0x0C)), reg16(0x10), reg16(0x12),
            static_cast<int16_t>(reg16(0x14)), reg16(0x2A), reg16(0x2C), reg16(0x2E), reg16(0x3C), reg16(0x3A),
            halGaugeCapacity.status(), millis());
#endif
      } else if (cmd == "HOME") {
        activityManager.goHome();
      } else if (cmd == "SLEEP") {
        enterDeepSleep();
      } else if (cmd.startsWith("OPEN_BOOK ")) {
        // CMD:OPEN_BOOK <duong/dan>: mo dung mot cuon de nghiem thu (chi co trong
        // ban nghiem thu qua USB). Duong dan tinh tu goc the nho.
        const String duongDan = cmd.substring(10);
        if (duongDan.startsWith("/")) {
          activityManager.goToReader(duongDan.c_str(), fastFirstPaint(BoardConfig::isX4Pro(), ReaderOpen::FromMenu));
          logSerial.printf("OPEN_BOOK:%s\n", duongDan.c_str());
        } else {
          logSerial.printf("OPEN_BOOK:INVALID\n");
        }
      } else if (cmd == "READ_RECENT") {
        const auto& books = RECENT_BOOKS.getBooks();
        if (!books.empty()) {
          activityManager.goToReader(books.front().path, fastFirstPaint(BoardConfig::isX4Pro(), ReaderOpen::FromMenu));
          // Named on the cable so a test can reopen this book afterwards and leave Recent as found.
          logSerial.printf("READ_RECENT:%s\n", books.front().path.c_str());
        }
      } else if (cmd == "BOOK_STATS") {
        const auto& books = RECENT_BOOKS.getBooks();
        if (!books.empty())
          activityManager.pushActivity(makeUniqueNoThrow<BookStatsActivity>(renderer, mappedInputManager,
                                                                            books.front().path, books.front().title));
#ifdef TENOR_UI_ACCEPTANCE
      } else if (cmd == "QUOTES_DUMP") {
        // Every quote file, name and raw bytes, so a card's quotes can be backed up over USB:
        // the web transfer keeps hidden folders off limits.
        auto dir = Storage.open("/.crosspoint/quotes");
        unsigned files = 0;
        if (dir && dir.isDirectory()) {
          char fileName[64];
          uint8_t chunk[256];
          for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
            if (entry.isDirectory()) continue;
            entry.getName(fileName, sizeof(fileName));
            logSerial.printf("QDUMP_FILE:%s:%u\n", fileName, static_cast<unsigned>(entry.size()));
            for (int n = entry.read(chunk, sizeof(chunk)); n > 0; n = entry.read(chunk, sizeof(chunk)))
              logSerial.write(chunk, n);
            logSerial.printf("\nQDUMP_END\n");
            ++files;
          }
        }
        logSerial.printf("QDUMP_DONE:%u\n", files);
#endif
      } else if (cmd == "QUOTES") {
        activityManager.pushActivity(makeUniqueNoThrow<QuotesActivity>(renderer, mappedInputManager));
      } else if (cmd == "CLOCK_SYNC") {
        activityManager.pushActivity(makeUniqueNoThrow<ClockSyncActivity>(renderer, mappedInputManager));
      } else if (cmd == "SETTINGS_READER") {
        activityManager.pushActivity(makeUniqueNoThrow<SettingsActivity>(
            renderer, mappedInputManager, static_cast<int>(settingstabs::Tab::READER), true));
      } else if (cmd == "STATUS_BAR_SETTINGS") {
        activityManager.pushActivity(makeUniqueNoThrow<StatusBarSettingsActivity>(renderer, mappedInputManager));
      } else if (cmd == "MEMORY") {
        logSerial.printf("MEMORY:%u,%u,%u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
#ifndef SIMULATOR
      } else if (cmd == "HEAP_MAP") {
        heapMapDump("command");
      } else if (cmd == "HEAP_INFO") {
        // Summary only: a per-block dump over serial tripped the watchdog.
        multi_heap_info_t info;
        heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
        logSerial.printf("HEAP_INFO:free=%u,alloc=%u,largest=%u,min=%u,blocks_alloc=%u,blocks_free=%u,loop_stack_free=%u\n",
                         static_cast<unsigned>(info.total_free_bytes), static_cast<unsigned>(info.total_allocated_bytes),
                         static_cast<unsigned>(info.largest_free_block), static_cast<unsigned>(info.minimum_free_bytes),
                         static_cast<unsigned>(info.allocated_blocks), static_cast<unsigned>(info.free_blocks),
                         static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
#ifdef TENOR_UI_ACCEPTANCE
        logSerial.printf("STACK_INFO:render_free=%u,ble_start_min_free=%u,ble_busy=%d,ble_initializing=%d\n",
                         static_cast<unsigned>(activityManager.renderStackHighWaterMark()),
                         static_cast<unsigned>(bleturner::startStackLeft()),
                         bleturner::status().starting || bleturner::status().running || bleturner::status().stopping,
                         bleturner::status().starting);
#endif
#endif
      } else if (cmd == "NETWORK") {
        logSerial.printf("NETWORK:status=%d,rssi=%d,ip=%s,heap=%u,largest=%u\n", static_cast<int>(WiFi.status()),
                         WiFi.RSSI(), WiFi.localIP().toString().c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#ifndef SIMULATOR
        logSerial.printf("NETWORK:gateway=%s,dns0=%s,dns1=%s\n", WiFi.gatewayIP().toString().c_str(),
                         WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str());
      } else if (cmd == "HEAP") {
        logSerial.printf("HEAP_START\n");
        heap_caps_dump(MALLOC_CAP_8BIT);
        logSerial.printf("HEAP_END\n");
#endif
#ifdef TENOR_TTF_PROBE
      } else if (cmd.startsWith("TTF_PROBE ")) {
        // CMD:TTF_PROBE <duong/dan.ttf> <pt> <so byte bo dem doc>
        // Do chi phi to chu TTF ngay tren chip nay. Khong dinh gi toi duong doc sach.
        String phanConLai = cmd.substring(10);
        phanConLai.trim();
        // Hai muc cuoi la co chu va be rong bo dem; phan dau la duong dan, va duong dan
        // co the co khoang trang, nen do nguoc tu cuoi len. Ban gia lap khong co
        // lastIndexOf hai tham so, nen di tay.
        const int khoangTrangCuoi = phanConLai.lastIndexOf(' ');
        int khoangTrangGiua = -1;
        for (int i = khoangTrangCuoi - 1; i >= 0; --i) {
          if (phanConLai.charAt(i) == ' ') {
            khoangTrangGiua = i;
            break;
          }
        }
        if (khoangTrangGiua <= 0) {
          logSerial.printf("TTF_PROBE_ERR:thieu tham so\n");
        } else {
          const String duongDan = phanConLai.substring(0, khoangTrangGiua);
          const int pt = phanConLai.substring(khoangTrangGiua + 1, khoangTrangCuoi).toInt();
          const int demRong = phanConLai.substring(khoangTrangCuoi + 1).toInt();
          const auto kq =
              ttfprobe::chayTrenTaskRieng(duongDan.c_str(), static_cast<uint8_t>(pt), static_cast<uint16_t>(demRong));
          if (!kq.moDuoc) {
            logSerial.printf("TTF_PROBE_ERR:%s\n", kq.loi ? kq.loi : "khong ro");
          } else {
            logSerial.printf(
                "TTF_PROBE:font=%s,pt=%d,dem=%d,to=%u,thieu=%u,diemanh=%u,"
                "ms_mo=%u,ms_co=%u,ms_to=%u,ms_tong=%u,nhuong=%u,"
                "dinh_ft=%u,heap_truoc=%u,heap_thap=%u,heap_sau=%u,"
                "ngan_xep_cap=%u,ngan_xep_con=%u,doc_the=%u,byte_the=%u\n",
                duongDan.c_str(), pt, demRong, kq.soChuToDuoc, kq.soChuThieu, kq.tongDiemAnh, kq.msMoFont, kq.msDatCo,
                kq.msToChu, kq.msTong, kq.soLanNhuong, kq.dinhBoNhoFt, kq.heapTruoc, kq.heapThapNhat, kq.heapSau,
                kq.nganXepCap, kq.nganXepConDu, kq.soLanDocThe, kq.soByteDocThe);
          }
        }
#endif  // TENOR_TTF_PROBE
#ifndef SIMULATOR
      } else if (cmd == "BUTTON_ADC") {
        int group1, group2;
        gpio.readButtonAdc(group1, group2);
        logSerial.printf("BUTTON_ADC:%d,%d\n", group1, group2);
#endif
      }
    }
  }

  // Hai dong ho, khong phai mot. `lastActivityTime` chi do NGUOI dung cham vao may,
  // va no lai la thu quyet dinh co ha xung CPU hay khong (xem nhanh
  // IDLE_POWER_SAVING_MS ben duoi). Truoc day mot man dang ban cung day dong ho nay
  // moi vong lap, nen viec giu may THUC vo tinh tat luon ca viec HA XUNG: may cam
  // cui chay het toc do trong khi chang ai dung.
  // `lastSleepResetTime` do rieng cho quyet dinh tu ngu, va man dang ban van day duoc.
  static unsigned long lastActivityTime = millis();
  static unsigned long lastSleepResetTime = millis();
  const bool nguoiDungChamVao = gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity() ||
                                pendingTiltActivity || halTiltSensor.hadActivity();
#if CROSSPOINT_BLE_HID_HOST
  const bool userActivity = nguoiDungChamVao || bleInputActivity;
#else
  const bool userActivity = nguoiDungChamVao;
#endif
  if (userActivity) {
    autoSleepBlockedUntilInput = false;
    if (gpio.wasAnyPressed()) LOG_INF("IN", "press t=%lu", static_cast<unsigned long>(millis()));
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }
  // Man dang ban giu may thuc, va khi no xong thi dong ho ngu dem lai tu luc do chu
  // khong ngu ngay lap tuc.
  if (userActivity || activityManager.preventAutoSleep()) lastSleepResetTime = millis();

  // Let wake continue as soon as its hold has been verified. The release can
  // arrive after setup, so consume that one input frame rather than making it
  // a page turn, refresh, or other short power-button action.
  if (wakePowerReleasePending && !gpio.isPressed(HalGPIO::BTN_POWER)) {
    wakePowerReleasePending = false;
    return;
  }

  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

  // Consume the second X4 Pro power-button release so it does not also run a
  // configured short-power action after toggling the frontlight.
  if (handleX4ProFrontlightDoubleClick()) {
    return;
  }

  const bool x4ProDoubleClickPwrLight = BoardConfig::isX4Pro() && SETTINGS.doubleClickPwrLight;

#if FREEINK_CAP_TOUCH
  // A single X4 Pro power click becomes Confirm only after the frontlight
  // double-click window expires without a second click.
  mappedInputManager.setPowerConfirmClickFrame(false);
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM && x4ProDoubleClickPwrLight) {
    if (lastX4ProPowerClickAt != 0 && millis() - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
      lastX4ProPowerClickAt = 0;
      mappedInputManager.setPowerConfirmClickFrame(true);
    }
    // A release held too long to be a double-click candidate (but still within
    // the normal Confirm press duration) never reaches handleX4ProFrontlightDoubleClick's
    // click tracking above, so it needs its own Confirm check here.
    if (mappedInputManager.wasReleased(MappedInputManager::Button::Power) &&
        gpio.getPowerButtonHeldTime() > X4PRO_POWER_CLICK_MAX_HOLD_MS &&
        gpio.getPowerButtonHeldTime() <= SETTINGS.getPowerButtonDuration()) {
      mappedInputManager.setPowerConfirmClickFrame(true);
    }
  }
#endif

  // Same deferral for SLEEP: getPowerButtonDuration() drops to 10ms so a quick
  // tap sleeps the device, which otherwise fires on button-down and never lets
  // a second click land. Sleep only once the double-click window has passed.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP && x4ProDoubleClickPwrLight &&
      lastX4ProPowerClickAt != 0 && millis() - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = 0;
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (sleepTimeoutMs > 0 && !autoSleepBlockedUntilInput &&
      millis() - lastSleepResetTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // A hold that woke the device must be released before it can count as a new
  // in-app long press. Otherwise a user who keeps holding after wake would put
  // the device straight back to sleep once allowSleepAt expires.
  static bool powerReleasedSinceWake = false;
  if (!gpio.isPressed(HalGPIO::BTN_POWER)) powerReleasedSinceWake = true;

  // On X4 Pro with SLEEP, a press still within the click window is a
  // double-click candidate - let it be released and evaluated above instead
  // of sleeping on button-down.
  const bool x4ProAwaitingClickWindow = x4ProDoubleClickPwrLight &&
                                        SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP &&
                                        gpio.getPowerButtonHeldTime() <= X4PRO_POWER_CLICK_MAX_HOLD_MS;

  if (!x4ProAwaitingClickWindow && powerReleasedSinceWake && millis() >= allowSleepAt &&
      gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    LOG_DBG("MAIN", "Power button held %lums, sleeping", gpio.getPowerButtonHeldTime());
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

#if FREEINK_DEVICE_PAPERMONO
  // Paper Mono reports the PMIC power button as a one-tick click, so the held
  // path above cannot fire. With the default Ignore action, retain the normal
  // power-button meaning and shut down; explicit alternate bindings still win.
  if (CrossPointSettings::powerClickSleeps(SETTINGS.shortPwrBtn) && millis() >= allowSleepAt &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    enterDeepSleep();
    return;
  }
#endif

  // Short power press, hard shake, face down, face up and the double taps: the same actions, one
  // decision (quickaction::resolve). A release before the first frame is on the glass is dropped:
  // the action would run on top of the frame being drawn and flash the screen once more.
  if (mappedInputManager.wasReleased(MappedInputManager::Button::Power) && activityManager.hasDrawnFrame()) {
    runQuickAction(SETTINGS.shortPwrBtn, quickaction::Trigger::PowerRelease);
  }
  if (halTiltSensor.wasShaken()) {
    runQuickAction(quickaction::shakeAsPowerAction(SETTINGS.shakeAction), quickaction::Trigger::Shake);
  }
  if (halTiltSensor.wasTurnedFaceDown()) {
    runQuickAction(quickaction::shakeAsPowerAction(SETTINGS.faceDownAction), quickaction::Trigger::FaceDown);
  }
  if (halTiltSensor.wasTurnedFaceUp()) {
    runQuickAction(quickaction::shakeAsPowerAction(SETTINGS.faceUpAction), quickaction::Trigger::FaceUp);
  }
  if (halTiltSensor.wasDoubleTapped()) {
    runQuickAction(quickaction::shakeAsPowerAction(SETTINGS.doubleTapAction), quickaction::Trigger::DoubleTap);
  }
  if (halTiltSensor.wasScreenTapped()) {
    runQuickAction(quickaction::shakeAsPowerAction(SETTINGS.screenTapAction), quickaction::Trigger::ScreenTap);
  }
  if (halTiltSensor.wasEdgeTapped()) {
    runQuickAction(quickaction::shakeAsPowerAction(SETTINGS.edgeTapAction), quickaction::Trigger::EdgeTap);
  }
  // Home key shortcuts (a board with a Home key; on X3 and X4 the action is always Ignore).
  if (mappedInputManager.homeButtonAction() == HomeButtonAction::ToggleFrontlight) {
    toggleFrontlight();
  }
  if (mappedInputManager.homeButtonAction() == HomeButtonAction::Refresh) {
    runQuickAction(CrossPointSettings::FORCE_REFRESH, quickaction::Trigger::PowerRelease);
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  // Not while reading: there a repaint is a full page re-render (visible
  // flash, the AA pass re-running, and a frontlight dip under the refresh
  // load). The EPUB reader redraws its status bar alone instead
  // (EpubReaderActivity::repaintStatusBarAlone); the other readers pick the
  // charging state up on the next page turn.
  if (gpio.wasUsbStateChanged() && !activityManager.isReaderActivity()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

#ifndef SIMULATOR
  // From the end of the first pass on, buttons are sampled off this loop, so a long
  // pass (page prewarm, section build) no longer swallows a short press. Setup and
  // this first pass have already absorbed any button held through boot.
  gpio.startBackgroundSampling();
#endif

  bool skipLoopDelay = false;
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) {
      // Let rendering advance without treating lock contention as idle.
      delay(10);
      return;
    }
    skipLoopDelay = activityManager.skipLoopDelay();
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (skipLoopDelay) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
#if CROSSPOINT_BLE_HID_HOST
    // The BLE controller needs a steady clock: dropping the CPU to 80 MHz while
    // it connects ended in an HCI ack failure and an interrupt watchdog reset
    // (X3, 18/09/2026). Keep full speed while the radio is up.
    const auto ble = bleturner::status();
    const bool radioActive = ble.starting || ble.running || ble.stopping;
#else
    const bool radioActive = false;
#endif
    if (!radioActive && millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      // Sleep in short slices and wake the poll as soon as a button contact closes.
      // InputManager commits a press only when two consecutive polls agree, so a
      // press shorter than one 50 ms sleep could land in a single sample and be lost.
      const unsigned long idleStart = millis();
      while (millis() - idleStart < 50) {
        delay(10);
        if (gpio.rawInputActive()) break;
      }
    } else {
      if (radioActive) powerManager.setPowerSaving(false);
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
