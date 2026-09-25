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
#ifdef TENOR_OTA_ACCEPTANCE
#include "activities/settings/OtaUpdateActivity.h"
#endif
#include <Arduino.h>
#include <BoardConfig.h>
#include <Epub.h>
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
#include <WiFi.h>
#include <XteinkDetect.h>
#include <builtinFonts/all.h>
#if FREEINK_CAP_TOUCH
#include <esp_sntp.h>
#endif

#include <Memory.h>
#ifdef TENOR_TTF_PROBE
#include <TtfProbe.h>
#endif

#include <cstring>
#ifndef SIMULATOR
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#endif

#include "CrossPointSettings.h"
#include "BleIdleOff.h"
#include "BlePageTurnerRuntime.h"
#include "SettingsList.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/home/BookStatsActivity.h"
#include "QuoteStore.h"
#include "activities/home/QuotesActivity.h"

// Page turner BLE: chi lien ket host cua SDK khi capability duoc bat (xem env x3-ble).
#include "FileTransferState.h"
#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
#include <BleKeyboardHost.h>
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
#include "platform/UsbSerialJtagHandoff.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"
#include "util/WakeBook.h"

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

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
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

// Font he thong: Geist cam tieu de, Be Vietnam Pro cam chu than va nhan nut.
// Ca hai deu du dau tieng Viet; do 14/09 tren 44 ky tu nguyen am hai dau.
EpdFont smallFont(&bevietnampro_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&bevietnampro_10_regular);
EpdFont ui10BoldFont(&bevietnampro_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&geist_12_regular);
EpdFont ui12BoldFont(&geist_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
RTC_NOINIT_ATTR uint32_t silentRebootHomeMenu;

#ifdef TENOR_PRESS_PROBE
#include <BatteryMonitor.h>
#include <esp_sleep.h>
// CMD:WAKE_TIMER <s>: every later sleep also arms a timed wake, which boots like a
// power-button wake. The RTC clock runs through deep sleep, so the time from the
// wake to the first app instruction is measured, bootloader included.
RTC_NOINIT_ATTR uint32_t probeWakeMagic;
RTC_NOINIT_ATTR uint32_t probeWakeSeconds;
RTC_NOINIT_ATTR uint64_t probeWakeTargetUs;
constexpr uint32_t PROBE_WAKE_MAGIC = 0x57414B45;
extern "C" uint64_t esp_rtc_get_time_us(void);
#endif
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;

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

#if FREEINK_CAP_TOUCH
static bool finishWifiSessionWithoutRestart() {
  if (!BoardConfig::hasTouch()) return false;

  // A software reset does not cycle externally powered touch/frontlight rails.
  // Shut down the network stack in place so those peripherals retain state.
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  WiFi.mode(WIFI_OFF);
  delay(100);
  LOG_DBG("MAIN", "WiFi stopped without restart on touch device");
  return true;
}
#endif

void silentRestart() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
#if FREEINK_CAP_TOUCH
  if (finishWifiSessionWithoutRestart()) return;
#endif
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootHomeMenu = static_cast<uint32_t>(activityManager.homeMenuOrigin());
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  // E-ink retains the previous frame until Home's first paint lands (~2-3s).
  // Without an overlay, users don't see the reboot and fire input through to
  // Home. Select on the default selectorIndex=0 then opens the most-recent
  // book, looking like a trampoline back to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestartToReader() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
#if FREEINK_CAP_TOUCH
  if (finishWifiSessionWithoutRestart()) return;
#endif
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void restartToHomeAfterStorageHandoff() {
  if (deepSleepInProgress) return;  // sleeping supersedes the storage handoff reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootHomeMenu = static_cast<uint32_t>(activityManager.homeMenuOrigin());
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Restart after storage handoff (target=home)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  handoffUsbOtgToSerialJtag();
  ESP.restart();
}

bool handleX4ProFrontlightDoubleClick() {
  if (!BoardConfig::isX4Pro() || !gpio.wasReleased(HalGPIO::BTN_POWER)) {
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
  const bool lightOn = !Frontlight.isOn();
  Frontlight.setOn(lightOn);
  SETTINGS.frontlightOn = lightOn ? 1 : 0;
  SETTINGS.saveToFile();
  LOG_INF("LIGHT", "Frontlight toggled %s by power-button double-click", lightOn ? "on" : "off");
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

  // Absolute gray images leave their MSB plane, a B/W threshold of the final
  // image. X3 can restore that baseline before its first clean wake refresh.
  // Overlay gray masks do not represent the whole image.
  const bool absoluteSleepFrame = gpio.deviceIsX3() && display.getController() == HalDisplay::Controller::UC8279 &&
                                  SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM;
  if (isQuickResumeSleep || absoluteSleepFrame ||
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TENOR) {
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
  display.deepSleep();
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

void setupDisplayAndFonts(bool seamless = false) {
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

  // Discover and load SD card fonts
  sdFontSystem.begin(renderer);
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
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();
  // checkPanic() clears the watchdog capture marker after a successful SD
  // dump, so retain the boot classification for the later activity route.
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too - RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_READER) ? silentRebootTarget : 0;
  const HomeMenuItem snapshotHomeMenu =
      isSilentReboot && silentRebootHomeMenu <= static_cast<uint32_t>(HomeMenuItem::FAVORITES_TAB)
          ? static_cast<HomeMenuItem>(silentRebootHomeMenu)
          : HomeMenuItem::NONE;
  silentRebootMagic = 0;
  silentRebootTarget = 0;
  silentRebootHomeMenu = 0;

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
  const unsigned long verifyStarted = millis();
  bool wakeHoldVerified = wakeupReason != HalGPIO::WakeupReason::PowerButton || gpio.verifyPowerButtonWakeup();
#ifdef TENOR_PRESS_PROBE
  LOG_INF("PROBE", "wake verify=%lu ms", millis() - verifyStarted);
  wakeHoldVerified = wakeHoldVerified || probeTimerWake;
#else
  (void)verifyStarted;
#endif

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
  // user's saved choice (either style) still wins.
  if (gpio.hasTouch()) {
    SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  }
  SETTINGS.loadFromFile();
  logHeapMark("store-settings");
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
  // reboots preserve the live state so they do not unexpectedly go dark.
  const bool restoreLightOn = SETTINGS.frontlightOn != 0 && (SETTINGS.frontlightRestoreOnWake != 0 || isSilentReboot);
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth, restoreLightOn);

  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      // With Short Power Button Press = Sleep, a single click wakes on any
      // device, X3 included, so the tap that locks also unlocks; otherwise the
      // button must still be held (ghost-wake debounce).
      if (!CrossPointSettings::acceptPowerWake(SETTINGS.shortPwrBtn, wakeHoldVerified)) {
        LOG_DBG("MAIN", "Power-button wake not held through verification, sleeping");
        halTiltSensor.deepSleep();
        Storage.prepareForDeepSleep();
        sleepUntilPowerButton();
      }
      wakePowerReleasePending = true;
      break;
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

  setupDisplayAndFonts(resume != BootResume::Splash);
  logHeapMark("display-and-fonts");

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::SplashlessWake: {
      // One-shot flag: re-arm the splash for the next ordinary boot. Set here,
      // written once the first frame is up (HomeActivity::render); every other
      // first screen writes it now, before it paints. Until then the card still
      // says "asleep", so a wake cut off before its first frame (power lost, a
      // hang) is simply repeated from the same kept frame. Writing in memory
      // first means a sleep started meanwhile clears it again, and whichever
      // save runs last writes that. A crash reboot shows the splash and
      // re-arms the flag below.
      APP_STATE.showBootScreen = true;
      if (recoveryFirmwareMode || rebootedFromPanic || wakeToBook) APP_STATE.saveToFile();
      const uint32_t wakeStarted = millis();
      if (Storage.exists(SLEEP_FRAME_FILE) && loadSleepFrameBuffer()) {
        if (gpio.deviceIsX3()) {
          // Restore controller RAM without activating a waveform. The first
          // Home/Reader paint cleans directly from this retained sleep frame.
          renderer.cleanupGrayscaleWithFrameBuffer();
        }
        LOG_DBG("MAIN", "Restored sleep frame baseline");
      }
      LOG_INF("BOOT", "Wake frame=%lu ms", static_cast<unsigned long>(millis() - wakeStarted));
      needsWakeRefresh = true;
      break;
    }
    case BootResume::Splash:
      if (!APP_STATE.showBootScreen) {
        APP_STATE.showBootScreen = true;
        APP_STATE.saveToFile();
      }
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
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
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
    activityManager.requestUpdateAndWait();
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
    const esp_err_t verified = esp_ota_mark_app_valid_cancel_rollback();
    LOG_INF("OTA", "Boot self-check complete: %s", esp_err_to_name(verified));
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
    info.valuePtr = &CrossPointSettings::blePageTurnerEnabled;
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

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
  mappedInputManager.update();
#ifndef SIMULATOR
  // The render task draws the gauge reading polled here (at most every 1.5 s).
  powerManager.pollGauge();
#endif

#if CROSSPOINT_BLE_HID_HOST
  // Resolve the radio handoff before USB's early return. Activity onEnter() can
  // already have done blocking storage or Wi-Fi work before this loop resumes.
  bool bleInputActivity = false;
  static bool bleReaderBeginAttempted = false;
  static bool bleReaderReconnectConfigured = false;
  // A start refused for memory is retried a few times once the reader has
  // settled: the first page build holds the heap the radio needs and gives it
  // back a few seconds later when the builder parks.
  static constexpr unsigned long BLE_READER_RETRY_MS = 5000;
  static constexpr uint8_t BLE_READER_RETRY_LIMIT = 6;
  static unsigned long bleReaderRetryAtMs = 0;
  static uint8_t bleReaderRetries = 0;
  static uint32_t bleReaderGeneration = 0;
  auto& bleHid = freeink::BleKeyboardHost::getInstance();
  static uint32_t lastBleCleanupMs = 0;
  static uint32_t bleIdleSinceMs = 0;
  // Remote with a button table (learned, or the built-in default for its kind): the
  // table picked once per link, and the tap/hold decision still waiting.
  static blebinding::Router bleRouter;
  if (bleHid.isStopping() && millis() - lastBleCleanupMs >= 250) {
    lastBleCleanupMs = millis();
    freeink::ble::suspendForTransition();  // Poll cancellation without blocking input.
  }
  const bool dangChiemStorage = activityManager.requiresExclusiveStorageLoop() || filetransfer::isActive();
  if (dangChiemStorage) {
    bleIdleSinceMs = 0;
    freeink::ble::suspendForTransition();
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
  const bool foregroundReader = activityManager.isForegroundReaderActivity();
  const bool foregroundActivityManagesTiltSensor = activityManager.isForegroundActivityManagingTiltSensor();
  updateTiltSensorForForegroundActivity(foregroundReader, foregroundActivityManagesTiltSensor);

#if CROSSPOINT_BLE_HID_HOST
  // Page turner BLE: callbacks only queue HID reports. Map each new report on
  // main, then let the foreground reader coalesce one pending direction in its
  // current activity generation. Unmapped/modifier reports do not reset timers.
  {
    // A saved opt-in starts only after the foreground reader has produced a
    // page. Home needs its own cover/font memory before we can assess BLE's
    // headroom. Each reader visit gets one attempt, avoiding allocation churn
    // after a low-memory rejection. Pairing remains an explicit settings action.
    const bool foregroundReader = activityManager.isForegroundReaderActivity();
    const uint32_t generation = activityManager.activityGeneration();
    if (!foregroundReader || !SETTINGS.blePageTurnerEnabled || generation != bleReaderGeneration) {
      // A build hold belongs to one reader visit; the next visit may start the radio again.
      if (generation != bleReaderGeneration) freeink::ble::setRadioHeldForBuild(false);
      bleReaderBeginAttempted = false;
      bleReaderReconnectConfigured = false;
      bleRouter = blebinding::Router();  // settings may have changed the tables while away
      bleReaderRetryAtMs = 0;
      bleReaderRetries = 0;
    }
    bleReaderGeneration = generation;
    // A page-key release or touch on the foreground reader grants one fresh
    // attempt after idle-off. Repeated idle ticks never allocate/retry BLE.
    const bool localReaderInput =
        mappedInputManager.wasReleased(MappedInputManager::Button::PageBack) ||
        mappedInputManager.wasReleased(MappedInputManager::Button::PageForward) ||
        mappedInputManager.wasReleased(MappedInputManager::Button::Left) ||
        mappedInputManager.wasReleased(MappedInputManager::Button::Right) || gpio.wasTouchActivity();
    // A radio the reader stopped for a starved build restarts on the reader's request once
    // its page is shown. Restarting it on a key release starved the build again and the
    // start task held this loop for 2.85 s (X3, 23/09/2026).
    if (foregroundReader && SETTINGS.blePageTurnerEnabled && freeink::ble::idleStopped() &&
        ((localReaderInput && !freeink::ble::radioHeldForBuild()) || freeink::ble::takeRearmRequest())) {
      freeink::ble::setIdleStopped(false);
      bleReaderBeginAttempted = false;
      bleReaderReconnectConfigured = false;
      LOG_INF("BLE", "Reader input rearmed idle radio");
    }
    if (dangChiemStorage || !SETTINGS.blePageTurnerEnabled) {
      bleIdleSinceMs = 0;
      freeink::ble::suspendForTransition();
    } else {
      if (foregroundReader && bleReaderBeginAttempted && freeink::ble::readerStartDeferred() && !bleHid.isRunning() &&
          !freeink::ble::initializing() && !freeink::ble::idleStopped() && bleReaderRetries < BLE_READER_RETRY_LIMIT) {
        if (bleReaderRetryAtMs == 0) {
          bleReaderRetryAtMs = millis() + BLE_READER_RETRY_MS;
        } else if (millis() >= bleReaderRetryAtMs) {
          bleReaderRetryAtMs = 0;
          ++bleReaderRetries;
          bleReaderBeginAttempted = false;
          LOG_INF("BLE", "Retrying reader BLE start after memory refusal (%u)", static_cast<unsigned>(bleReaderRetries));
        }
      } else {
        bleReaderRetryAtMs = 0;
        if (bleHid.isRunning()) bleReaderRetries = 0;
      }
      // A book still building its index in the background keeps the radio off until the index
      // is whole (EpubReaderActivity::holdsRadio); the device keys work meanwhile.
      if (foregroundReader && activityManager.isForegroundReaderReady() && !bleReaderBeginAttempted &&
          !freeink::ble::idleStopped() && !bleHid.isStopping() && !activityManager.foregroundReaderHoldsRadio()) {
        bleReaderBeginAttempted = true;
        if (!bleHid.isRunning()) {
          const bool started = freeink::ble::beginAsync(renderer);
          freeink::ble::setReaderStartDeferred(!started);
          if (started) {
            LOG_INF("BLE", "Reader BLE start requested");
          } else {
            LOG_ERR("BLE", "Reader BLE start deferred: insufficient memory or unavailable radio");
          }
        }
      }
      // Lau khong ai noi thi ha radio xuong. Khong co moc nay thi bat mot lan la
      // radio chay mai, ma vong tiet kiem dien ben duoi co chu y giu CPU o toc do
      // day chung nao radio con song, nen may nam im van an pin. Luat o BleIdleOff.h.
      if (!freeink::ble::initializing() && bleHid.isRunning()) {
        if (bleHid.isConnected() || bleIdleSinceMs == 0) bleIdleSinceMs = millis();
        if (bleidle::shouldStop(true, bleHid.isConnected(), millis() - bleIdleSinceMs)) {
          LOG_INF("BLE", "Radio idle for %u ms with nothing connected; stopping", bleidle::kIdleOffMs);
          freeink::ble::stopForIdle();
          bleIdleSinceMs = 0;
        }
      } else {
        bleIdleSinceMs = 0;
      }

      if (foregroundReader && !freeink::ble::initializing() && bleHid.isRunning()) {
        freeink::ble::setReaderStartDeferred(false);
        if (!bleReaderReconnectConfigured && !bleHid.isStopping()) {
          bleReaderReconnectConfigured = true;
          if (SETTINGS.blePeerAddr[0] != '\0' &&
              !bleHid.armSelectedPeerReconnect(SETTINGS.blePeerAddr)) {
            LOG_INF("BLE", "Selected reader peer was not armed");
          }
        }
        bleHid.poll();
        // A remote with a table goes by RAW edges: each edge checks at most 8 slots of
        // the connected remote's table, and a button the table does not name falls back
        // to the old usage mapping. Without a table the raw ring is only drained and the
        // key path below runs exactly as before.
        freeink::RawButtonEvent tho;
        bleRouter.follow(bleHid.isConnected());
        if (bleRouter.linked && !bleRouter.chosen) {
          // Edges queued while the reader was not in front (the remote stays linked on
          // Home) do not belong to this page: drop them, so an old press cannot skip a
          // chapter when the book opens.
          while (bleHid.popRawButton(tho)) {
          }
          bleRouter.table = blebinding::tableFor(SETTINGS.bleRemotes, SETTINGS.bleRemoteCount, bleHid.connectedAddr(),
                                                 bleHid.connectedName());
          bleRouter.chosen = true;
        }
        const bool quaBang = blebinding::routes(bleRouter.table);
        const auto lam = [&](const blebinding::Action a) {
          if (a == blebinding::Action::None) return;
          const bool toi = a == blebinding::Action::NextPage || a == blebinding::Action::NextChapter;
          const bool chuong = a == blebinding::Action::NextChapter || a == blebinding::Action::PrevChapter;
          if (chuong ? activityManager.chapterSkip(toi) : activityManager.pageTurn(toi)) bleInputActivity = true;
        };
        while (bleHid.popRawButton(tho)) {
          if (!quaBang) continue;
          const auto cu = SETTINGS.blePageActionFor(tho.keycode, tho.mods);
          const auto hanhDong = blebinding::onRawEdge(
              *bleRouter.table, tho.code(), tho.pressed, tho.atMs,
              cu == CrossPointSettings::BlePageAction::NextPage       ? blebinding::Action::NextPage
              : cu == CrossPointSettings::BlePageAction::PreviousPage ? blebinding::Action::PrevPage
                                                                      : blebinding::Action::None,
              bleRouter.wait);
          lam(hanhDong);  // act first, log after: the log line is not on the page's clock
          LOG_INF("BLE", "raw %u:%u=%02X %s -> %s", tho.reportId, tho.byteIndex, tho.value, tho.pressed ? "down" : "up",
                  blebinding::actionName(hanhDong));
        }
        if (quaBang) {
          const auto giu = blebinding::pollHold(bleRouter.wait, millis());
          lam(giu);
          if (giu != blebinding::Action::None) LOG_INF("BLE", "raw hold -> %s", blebinding::actionName(giu));
        }
        freeink::KeyEvent ev;
        while (bleHid.popKey(ev)) {
          // With a table the raw edge already decided this frame: its key event is only logged.
          const auto hanhDong =
              quaBang ? CrossPointSettings::BlePageAction::None : SETTINGS.blePageActionFor(ev.keycode, ev.mods);
          // Mot dong cho MOI phim lay ra: day la duong chan doan cho nguoi cam
          // dieu khien that (doc qua serial la biet remote gui ma nao).
          LOG_INF("BLE", "key 0x%02X mods 0x%02X %s -> %s", ev.keycode, ev.mods, ev.pressed ? "down" : "up",
                  hanhDong == CrossPointSettings::BlePageAction::PreviousPage  ? "previous"
                  : hanhDong == CrossPointSettings::BlePageAction::NextPage    ? "next"
                                                                               : "none");
          if (hanhDong != CrossPointSettings::BlePageAction::PreviousPage &&
              hanhDong != CrossPointSettings::BlePageAction::NextPage) {
            continue;
          }
          // Only the press edge acts. Free3 reports a fixed release 60-100 ms after
          // every press regardless of how long the button is held, so a hold cannot
          // be told from a tap and the release frame carries nothing to act on.
          if (!ev.pressed) continue;
          // Enqueue once per new mapped report. A deferred repaint never
          // generates another activity-timer reset on subsequent ticks.
          if (activityManager.pageTurn(hanhDong == CrossPointSettings::BlePageAction::NextPage)) {
            bleInputActivity = true;
          }
        }
      }
    }
  }
#endif

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
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
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      powerManager.setPowerSaving(false);
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
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
        const bool sideSwapped = SETTINGS.sideButtonLayout == CrossPointSettings::NEXT_PREV;
        int button = -1;
        if (sscanf(cmd.c_str() + 6, "%11s %u %u %u", name, &hold, &count, &gap) == 4) {
          const String n(name);
          if (n == "NEXT") button = SETTINGS.frontButtonRight;
          if (n == "PREV") button = SETTINGS.frontButtonLeft;
          if (n == "SIDE_NEXT") button = sideSwapped ? HalGPIO::BTN_UP : HalGPIO::BTN_DOWN;
          if (n == "SIDE_PREV") button = sideSwapped ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP;
          if (n == "POWER") button = HalGPIO::BTN_POWER;
          if (n == "BACK") button = SETTINGS.frontButtonBack;
          if (n == "CONFIRM") button = SETTINGS.frontButtonConfirm;
        }
        if (button >= 0) gpio.injectPresses(button, hold, count, gap);
        logSerial.printf("PRESS:button=%d,hold=%u,count=%u,gap=%u,t=%lu\n", button, hold, count, gap, millis());
#endif
#if defined(TENOR_UI_ACCEPTANCE) || defined(TENOR_PRESS_PROBE)
      } else if (cmd == "UI_READER_NEXT" || cmd == "UI_READER_PREV") {
        const bool queued = activityManager.pageTurn(cmd == "UI_READER_NEXT");
        logSerial.printf("UI_READER:synthetic_external=1,queued=%d,generation=%u\n", queued,
                         static_cast<unsigned>(activityManager.activityGeneration()));
#if CROSSPOINT_BLE_HID_HOST
      } else if (cmd == "BLE_TEST_BEGIN") {
        // In-memory only. A reset restores the saved user preference.
        SETTINGS.blePageTurnerEnabled = 1;
        const bool ok = freeink::ble::begin(renderer);
        logSerial.printf("BLE_TEST:begin=%d,heap=%u,largest=%u\n", ok, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      } else if (cmd == "BLE_TEST_END") {
        SETTINGS.blePageTurnerEnabled = 0;
        freeink::ble::setIdleStopped(false);
        freeink::ble::setReaderStartDeferred(false);
        const bool ended = freeink::ble::suspendForTransition(1000);
        logSerial.printf("BLE_TEST:end=%d,heap=%u,largest=%u\n", ended, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      } else if (cmd == "BLE_TEST_SCAN") {
        auto& host = freeink::BleKeyboardHost::getInstance();
        if (host.isRunning()) host.startScan(5000);
      } else if (cmd == "BLE_TEST_STATUS") {
        const auto& host = freeink::BleKeyboardHost::getInstance();
        logSerial.printf("BLE_TEST:enabled=%u,running=%d,scanning=%d,connected=%d,busy=%d,initializing=%d,heap=%u,largest=%u\n",
                         SETTINGS.blePageTurnerEnabled, host.isRunning(), host.isScanning(), host.isConnected(),
                         freeink::ble::busy(), freeink::ble::initializing(),
                         ESP.getFreeHeap(), ESP.getMaxAllocHeap());
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
        if (sscanf(cmd.c_str() + 10, "%31s %u %u", family, &point, &weight) == 3 && point >= 12 && point <= 26 &&
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
        // bytes it only reports `overflow`, the raw presses dropped since boot.
        uint8_t frame[16];
        size_t n = 0;
        const char* p = cmd.c_str() + 7;
        unsigned v = 0;
        int used = 0;
        while (n < sizeof(frame) && sscanf(p, " %x%n", &v, &used) == 1) {
          frame[n++] = static_cast<uint8_t>(v);
          p += used;
        }
        if (!bleHid.isConnected()) {
          bleRouter.table = blebinding::defaultTableFor("Free3");
          bleRouter.chosen = true;
        }
        const unsigned long t0 = micros();
        bleHid.onReportIngest(frame, n);
        logSerial.printf("BLE_RAW:len=%u,ingest_us=%lu,running=%d,overflow=%u,t=%lu\n", static_cast<unsigned>(n),
                         micros() - t0, bleHid.isRunning(), bleHid.rawOverflows(), millis());
#endif
#endif
#ifdef TENOR_PRESS_PROBE
      } else if (cmd.startsWith("WAKE_TIMER ")) {
        probeWakeSeconds = static_cast<uint32_t>(cmd.substring(11).toInt());
        probeWakeMagic = PROBE_WAKE_MAGIC;
        logSerial.printf("WAKE_TIMER:%u\n", static_cast<unsigned>(probeWakeSeconds));
      } else if (cmd == "LOGDUMP") {
        probeLogDump();
      } else if (cmd == "PANIC") {
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
          activityManager.goToReader(duongDan.c_str());
          logSerial.printf("OPEN_BOOK:%s\n", duongDan.c_str());
        } else {
          logSerial.printf("OPEN_BOOK:INVALID\n");
        }
      } else if (cmd == "READ_RECENT") {
        const auto& books = RECENT_BOOKS.getBooks();
        if (!books.empty()) {
          activityManager.goToReader(books.front().path);
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
                         static_cast<unsigned>(freeink::ble::startStackHighWaterMark()),
                         freeink::ble::busy(), freeink::ble::initializing());
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

#if FREEINK_CAP_TOUCH
  // A single X4 Pro power click becomes Confirm only after the frontlight
  // double-click window expires without a second click.
  mappedInputManager.setPowerConfirmClickFrame(false);
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM && BoardConfig::isX4Pro() &&
      lastX4ProPowerClickAt != 0 && millis() - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = 0;
    mappedInputManager.setPowerConfirmClickFrame(true);
  }
#endif

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

  if (powerReleasedSinceWake && millis() >= allowSleepAt && gpio.isPressed(HalGPIO::BTN_POWER) &&
      gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
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
  if ((SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP ||
       SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::IGNORE) &&
      millis() >= allowSleepAt && mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    enterDeepSleep();
    return;
  }
#endif

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  // Not while reading: there a repaint is a full page re-render (visible
  // flash, the AA pass re-running, and a frontlight dip under the refresh
  // load); the reader's status bar picks the charging state up on the next
  // page turn instead.
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

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
#if CROSSPOINT_BLE_HID_HOST
    // The BLE controller needs a steady clock: dropping the CPU to 80 MHz while
    // it connects ended in an HCI ack failure and an interrupt watchdog reset
    // (X3, 18/09/2026). Keep full speed while the radio is up.
    const bool radioActive = freeink::ble::busy();
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
