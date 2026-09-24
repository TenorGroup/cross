#include <BatteryMonitor.h>
#include <ButtonEdgeLatch.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <PanelMemo.h>
#include <PowerManager.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <XteinkDetect.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include <atomic>

// Global HalGPIO instance
HalGPIO gpio;

namespace X3GPIO {

bool readI2CReg16LE(uint8_t addr, uint8_t reg, uint16_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *outValue = (static_cast<uint16_t>(hi) << 8) | lo;
  return true;
}

bool readBQ27220CurrentMA(int16_t* outCurrent) {
  uint16_t raw = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_CUR_REG, &raw)) {
    return false;
  }
  *outCurrent = static_cast<int16_t>(raw);
  return true;
}

}  // namespace X3GPIO

namespace {
constexpr char HW_NAMESPACE[] = "cphw";
constexpr char NVS_KEY_DEV_OVERRIDE[] = "dev_ovr";  // 0=auto, 1=x4, 2=x3
constexpr char NVS_KEY_DEV_CACHED[] = "dev_det";    // 0=unknown, 1=x4, 2=x3

enum class NvsDeviceValue : uint8_t { Unknown = 0, X4 = 1, X3 = 2 };

NvsDeviceValue readNvsDeviceValue(const char* key, NvsDeviceValue defaultValue) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, true)) {
    return defaultValue;
  }
  const uint8_t raw = prefs.getUChar(key, static_cast<uint8_t>(defaultValue));
  prefs.end();
  if (raw > static_cast<uint8_t>(NvsDeviceValue::X3)) {
    return defaultValue;
  }
  return static_cast<NvsDeviceValue>(raw);
}

void writeNvsDeviceValue(const char* key, NvsDeviceValue value) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, false)) {
    return;
  }
  prefs.putUChar(key, static_cast<uint8_t>(value));
  prefs.end();
}

HalGPIO::DeviceType nvsToDeviceType(NvsDeviceValue value) {
  return value == NvsDeviceValue::X3 ? HalGPIO::DeviceType::X3 : HalGPIO::DeviceType::X4;
}

HalGPIO::DeviceType detectDeviceTypeWithFingerprint() {
  // Explicit override for recovery/support:
  // 0 = auto, 1 = force X4, 2 = force X3
  const NvsDeviceValue overrideValue = readNvsDeviceValue(NVS_KEY_DEV_OVERRIDE, NvsDeviceValue::Unknown);
  if (overrideValue == NvsDeviceValue::X3 || overrideValue == NvsDeviceValue::X4) {
    LOG_INF("HW", "Device override active: %s", overrideValue == NvsDeviceValue::X3 ? "X3" : "X4");
    return nvsToDeviceType(overrideValue);
  }

  const NvsDeviceValue cachedValue = readNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::Unknown);
  if (cachedValue == NvsDeviceValue::X3 || cachedValue == NvsDeviceValue::X4) {
    LOG_INF("HW", "Using cached device type: %s", cachedValue == NvsDeviceValue::X3 ? "X3" : "X4");
    return nvsToDeviceType(cachedValue);
  }

  // No cache yet: use FreeInk's canonical two-pass X3 fingerprint and persist
  // only confirmed results. Inconclusive probes deliberately remain uncached.
  uint8_t score1 = 0;
  uint8_t score2 = 0;
  const freeink::XteinkVerdict verdict = freeink::detectXteinkVerdict(&score1, &score2);
  LOG_INF("HW", "Xteink probe scores: pass1=%u pass2=%u verdict=%u", score1, score2, static_cast<unsigned>(verdict));

  if (verdict == freeink::XteinkVerdict::X3Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X3);
    return HalGPIO::DeviceType::X3;
  }

  if (verdict == freeink::XteinkVerdict::X4Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X4);
    return HalGPIO::DeviceType::X4;
  }

  // Conservative fallback for first boot with inconclusive probes.
  return HalGPIO::DeviceType::X4;
}

#ifdef TENOR_PRESS_PROBE
// Synthetic presses for on-device latency measurement. They enter through the
// SDK button hook, so they take the same debounce, held-time and edge path as
// a real contact: a press that no sample catches twice is lost like a real one.
struct PressPlan {
  unsigned long startMs;
  uint16_t holdMs;
  uint16_t periodMs;
  uint16_t count;
  uint16_t logged;
  int16_t lastSeen;
  uint8_t mask;
};
PressPlan pressPlan{};

// Runs inside the SDK button read, on the sample timer. No logging here: a
// full USB serial buffer must never stall the sampler it is measuring.
uint8_t plannedButtons() {
  if (pressPlan.count == 0 || static_cast<long>(millis() - pressPlan.startMs) < 0) return 0;
  const unsigned long since = millis() - pressPlan.startMs;
  const unsigned long k = since / pressPlan.periodMs;
  const bool down = k < pressPlan.count && since % pressPlan.periodMs < pressPlan.holdMs;
  if (down) pressPlan.lastSeen = static_cast<int16_t>(k);
  return down ? pressPlan.mask : 0;
}

// Main loop: one line per planned press once its contact is over, stamped with
// its physical start so latency is measured from the finger. seen=0 means no
// sample ran while the contact was closed.
void reportPlannedPresses() {
  if (pressPlan.count == 0 || static_cast<long>(millis() - pressPlan.startMs) < 0) return;
  const unsigned long since = millis() - pressPlan.startMs;
  while (pressPlan.logged < pressPlan.count &&
         since >= static_cast<unsigned long>(pressPlan.logged) * pressPlan.periodMs + pressPlan.holdMs) {
    LOG_INF("IN", "inject n=%u t=%lu seen=%d", pressPlan.logged + 1,
            pressPlan.startMs + static_cast<unsigned long>(pressPlan.logged) * pressPlan.periodMs,
            pressPlan.lastSeen >= static_cast<int16_t>(pressPlan.logged));
    ++pressPlan.logged;
  }
}
#endif

}  // namespace

#ifdef TENOR_PRESS_PROBE
void HalGPIO::injectPresses(const uint8_t buttonIndex, const uint16_t holdMs, const uint16_t count,
                            const uint16_t gapMs) {
  // Every press follows a gap, the first one included, so a single press with a
  // long gap lands on an idle, down-clocked loop.
  // The sample timer reads this plan; count goes last so it never sees a half-written one.
  pressPlan.count = 0;
  pressPlan.startMs = millis() + gapMs;
  pressPlan.holdMs = holdMs;
  pressPlan.periodMs = static_cast<uint16_t>(holdMs + gapMs);
  pressPlan.logged = 0;
  pressPlan.lastSeen = -1;
  pressPlan.mask = static_cast<uint8_t>(1u << buttonIndex);
  InputManager::setButtonHook(plannedButtons);
  std::atomic_thread_fence(std::memory_order_release);
  pressPlan.count = pressPlan.periodMs == 0 ? 0 : count;
}
#endif

#if FREEINK_MCU_C3
// Survives deep sleep, not power loss; read only on a deep-sleep wake (PanelMemo.h).
static RTC_NOINIT_ATTR panelmemo::Memo panelMemo;
#endif

void HalGPIO::begin() {
#if FREEINK_MCU_C3
  _deviceType = detectDeviceTypeWithFingerprint();
  BoardConfig::selectDevice(deviceIsX3() ? BoardConfig::Board::XteinkX3 : BoardConfig::Board::XteinkX4);

  // Resolve the per-batch controller before SPI owns the display pins. FreeInk
  // checks the OEM hw_calib/screenType value first, then falls back to its
  // two-pass display-bus probe. X3's facade keys panel selection off the sibling
  // board profile, so preserve a detected UC8279 through setDisplayX3().
  // A wake from deep sleep reuses the answer instead (PanelMemo.h). Only a clean
  // verdict is kept: an unknown ID or a BUSY timeout is probed again next time.
  uint8_t controller = 0, variant = 0;
  if (panelmemo::read(panelMemo, esp_reset_reason() == ESP_RST_DEEPSLEEP, controller, variant)) {
    BoardConfig::ACTIVE.displayController = static_cast<BoardConfig::DisplayController>(controller);
    BoardConfig::ACTIVE.displayControllerVariant = variant;
    LOG_INF("HW", "Panel controller kept from sleep: %u", controller);
  } else {
    panelMemo = {};
    freeink::applyXteinkDisplayController();
    const auto& probe = freeink::getXteinkDisplayProbeDiag();
    if (probe.valid && !probe.busyTimedOut &&
        probe.verdict != static_cast<uint8_t>(freeink::DisplayControllerVerdict::Inconclusive)) {
      panelMemo = panelmemo::make(static_cast<uint8_t>(BoardConfig::ACTIVE.displayController),
                                  BoardConfig::ACTIVE.displayControllerVariant);
    }
  }
  if (deviceIsX3() && BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    BoardConfig::selectDevice(BoardConfig::Board::XteinkX3Uc8279);
  }

  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);

  if (deviceIsX4()) {
    pinMode(BAT_GPIO0, INPUT);
    pinMode(UART0_RXD, INPUT);
  } else {
    // Wake classification reads the X3 fuel gauge before RTC/IMU startup.
    const auto& gauge = BoardConfig::ACTIVE.batteryGauge;
    if (!Wire.begin(gauge.i2cSda, gauge.i2cScl, gauge.i2cHz)) {
      LOG_ERR("GPIO", "Could not initialize X3 fuel-gauge I2C");
    }
  }
#else
  _deviceType = DeviceType::X4;
#endif
  inputMgr.begin();
}

namespace {
// Shared between the sample timer and the main loop; one HalGPIO exists. Once the
// timer runs, only it touches the SDK debounce. After each sample it publishes
// the edges and a snapshot of the level and held times under edgeLock. update()
// takes the edges and a copy of the snapshot its last edge came with under the same
// lock, and a loop pass reads levels and held times from its copy, so they always
// agree with its edges.
portMUX_TYPE edgeLock = portMUX_INITIALIZER_UNLOCKED;
ButtonFrames frames;
static_assert(ButtonFrames::kPowerButton == HalGPIO::BTN_POWER);

ButtonSample readSample(const InputManager& input) {
  ButtonSample now;
  for (uint8_t button = 0; button <= HalGPIO::BTN_POWER; ++button) {
    if (input.isPressed(button)) now.state |= 1u << button;
  }
  now.debouncePending = input.isDebouncePending();
  now.heldMs = input.getHeldTime();
  now.powerHeldMs = input.getPowerButtonHeldTime();
  now.atMs = millis();
  return now;
}

// Set while another reader (Back latch of file transfer, BUTTON_ADC) converts the
// button ladder. The IDF oneshot driver only try-locks the ADC unit and Arduino's
// analogRead then returns 0, which the ladder reads as Right+Down, so the timer
// skips that tick instead of feeding it to the debounce. A plain flag is enough on
// the single-core C3: the timer task outranks every other ladder reader.
std::atomic<bool> ladderInUse{false};
}  // namespace

void HalGPIO::startBackgroundSampling() {
  if (sampleTimer || inputMgr.hasTouch() ||
      BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::XteinkAdcLadder) {
    return;
  }
  // Ticks missed while the flash cache is off (OTA, settings save) are dropped, not replayed
  // back to back: one fresh sample after the gap is all the debounce needs.
  const esp_timer_create_args_t args{sampleButtons, this, ESP_TIMER_TASK, "buttons", true};
  esp_timer_handle_t timer = nullptr;
  if (esp_timer_create(&args, &timer) != ESP_OK) return;
  // Published before the first tick: from here on only the timer runs the debounce,
  // and the main loop reads the snapshot, seeded here with the state it had.
  frames.latest = frames.frame = readSample(inputMgr);
  // Kept only once it runs: with `sampleTimer` set, update() stops polling and reads the frames,
  // so a timer that never started would leave every button dead.
  if (esp_timer_start_periodic(timer, 10000) != ESP_OK) {
    esp_timer_delete(timer);
    return;
  }
  sampleTimer = timer;
}

void HalGPIO::sampleButtons(void* self) {
  if (ladderInUse.load(std::memory_order_acquire)) return;
  auto& input = static_cast<HalGPIO*>(self)->inputMgr;
  input.update();
  uint8_t pressed = 0;
  uint8_t released = 0;
  for (uint8_t button = 0; button <= BTN_POWER; ++button) {
    if (input.wasPressed(button)) pressed |= 1u << button;
    if (input.wasReleased(button)) released |= 1u << button;
  }
  const ButtonSample now = readSample(input);
  portENTER_CRITICAL(&edgeLock);
  frames.publish(pressed, released, now);
  portEXIT_CRITICAL(&edgeLock);
}

void HalGPIO::update() {
  if (sampleTimer) {
    portENTER_CRITICAL(&edgeLock);
    frames.beginFrame();
    portEXIT_CRITICAL(&edgeLock);
  } else {
    inputMgr.update();
  }
#ifdef TENOR_PRESS_PROBE
  reportPlannedPresses();
#endif
  if (usbPollTask == nullptr) usbPollTask = xTaskGetCurrentTaskHandle();
  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

void HalGPIO::readButtonAdc(int& group1, int& group2) {
  InputManager::ButtonAdcSample first{}, second{};
  sampleButtonAdc(first, second);
  group1 = first.raw;
  group2 = second.raw;
}

void HalGPIO::sampleButtonAdc(InputManager::ButtonAdcSample& first, InputManager::ButtonAdcSample& second) {
  ladderInUse.store(true, std::memory_order_release);
  inputMgr.readButtonAdc(first, second);
  ladderInUse.store(false, std::memory_order_release);
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  if (!sampleTimer) return inputMgr.isPressed(buttonIndex);
  portENTER_CRITICAL(&edgeLock);
  const bool pressed = frames.isPressed(buttonIndex);
  portEXIT_CRITICAL(&edgeLock);
  return pressed;
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  return sampleTimer ? (frames.framePressed >> buttonIndex) & 1u : inputMgr.wasPressed(buttonIndex);
}

bool HalGPIO::wasAnyPressed() const { return sampleTimer ? frames.framePressed != 0 : inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  return sampleTimer ? (frames.frameReleased >> buttonIndex) & 1u : inputMgr.wasReleased(buttonIndex);
}

bool HalGPIO::wasAnyReleased() const { return sampleTimer ? frames.frameReleased != 0 : inputMgr.wasAnyReleased(); }

bool HalGPIO::rawInputActive() {
  if (sampleTimer) {
    // The timer owns the ladder: wake on a collected edge or a contact the debounce is still weighing.
    portENTER_CRITICAL(&edgeLock);
    const bool active = frames.active();
    portEXIT_CRITICAL(&edgeLock);
    return active;
  }
  if (inputMgr.isPowerButtonPhysicallyPressed()) return true;
#ifdef TENOR_PRESS_PROBE
  if (plannedButtons()) return true;  // a synthetic contact wakes the idle poll like a real one
#endif
  InputManager::ButtonAdcSample g1{}, g2{};
  inputMgr.readButtonAdc(g1, g2);
  // The Xteink ladder idles at the ADC full-scale rail (~4095); every button band sits below 3900.
  constexpr int kIdleRailMin = 4000;
  return (g1.raw >= 0 && g1.raw < kIdleRailMin) || (g2.raw >= 0 && g2.raw < kIdleRailMin);
}

unsigned long HalGPIO::getHeldTime() const {
  if (!sampleTimer) return inputMgr.getHeldTime();
  portENTER_CRITICAL(&edgeLock);
  const unsigned long held = frames.heldTime(millis());
  portEXIT_CRITICAL(&edgeLock);
  return held;
}

unsigned long HalGPIO::getPowerButtonHeldTime() const {
  if (!sampleTimer) return inputMgr.getPowerButtonHeldTime();
  portENTER_CRITICAL(&edgeLock);
  const unsigned long held = frames.powerHeldTime(millis());
  portEXIT_CRITICAL(&edgeLock);
  return held;
}

bool HalGPIO::hasTouch() const { return inputMgr.hasTouch(); }

bool HalGPIO::hasHomeKey() const { return BoardConfig::hasHomeKey(); }

bool HalGPIO::wasHomeKeyTapped() const { return inputMgr.wasHomeKeyTapped(); }

bool HalGPIO::wasHomeKeyLongPressed() const { return inputMgr.wasHomeKeyLongPressed(); }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const { return inputMgr.wasTouchTap(nx, ny); }

bool HalGPIO::wasTouchDown(float& nx, float& ny) const { return inputMgr.wasTouchPressedAt(nx, ny); }

bool HalGPIO::wasTouchReleased() const { return inputMgr.wasTouchReleased(); }

bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
  return inputMgr.isTouchTapCandidate(nx, ny, heldMs);
}

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const { return inputMgr.isTouchHeldAt(nx, ny); }

bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const { return inputMgr.wasTouchLongPress(nx, ny); }

void HalGPIO::suppressTouchContact() { inputMgr.suppressTouchContact(); }

unsigned long HalGPIO::lastTouchHeldMs() const { return inputMgr.lastTouchHeldMs(); }

bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  return inputMgr.wasSwipe(nxStart, nyStart, nxEnd, nyEnd);
}

bool HalGPIO::wasTouchActivity() const { return inputMgr.wasTouchActivity(); }

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(const bool enabled) {
  InputManager::setSharedConfirmPowerShortPressEmitsPower(enabled);
}

bool HalGPIO::hasEdgeSideButtons() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Pro ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Classic;
}

bool HalGPIO::isXteinkDevice() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4;
}

bool HalGPIO::verifyPowerButtonWakeup() {
  // M5Paper v1.1: the classic ESP32's reset-to-setup() latency exceeds a normal
  // wheel click, so a click wake is always released before this samples and
  // verification would re-sleep on every wake. Its wheel has hard external
  // pull-ups, so the ghost-wake debounce this implements is not needed.
  if (BoardConfig::isPaperMono() || BoardConfig::isM5PaperV11() || BoardConfig::ACTIVE.input.power < 0) {
    return true;
  }

  const unsigned long POWER_WAKE_STABILITY_MS = deviceIsX3() ? 400 : 10;
  const bool heldAtFirstSample = inputMgr.isPowerButtonPhysicallyPressed();
  const unsigned long sampleStart = millis();
  inputMgr.update();
  while (millis() - sampleStart < POWER_WAKE_STABILITY_MS || inputMgr.isDebouncePending()) {
    delay(1);
    inputMgr.update();
    if (deviceIsX3() && !inputMgr.isPowerButtonPhysicallyPressed()) return false;
  }
  return heldAtFirstSample && inputMgr.isPowerButtonPhysicallyPressed();
}

bool HalGPIO::isUsbConnected() const {
  if (deviceIsX3()) {
    // Other tasks (the render task drawing a charging bolt) get the loop's last answer:
    // the gauge is only safe to read from one task, see HalPowerManager.
    if (usbPollTask != nullptr && xTaskGetCurrentTaskHandle() != usbPollTask) return lastUsbConnected;
    // X3: infer USB/charging via BQ27220 Current() register (0x0C, signed mA).
    // Positive current means charging.
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
      int16_t currentMa = 0;
      if (X3GPIO::readBQ27220CurrentMA(&currentMa)) {
        return currentMa > 0;
      }
      delay(2);
    }
    return false;
  }
  if (BoardConfig::ACTIVE.usbDetect >= 0) {
    return digitalRead(BoardConfig::ACTIVE.usbDetect) == HIGH;
  }
  // No digital USB-detect line (e.g. Sticky, whose PWR_IN_VOLT is an analog
  // divider): infer external power from charging state instead. BatteryMonitor
  // picks the board's best source - charger IC status, gauge Current() sign, or
  // a /STAT pin - and reports false on boards with no battery telemetry at all.
  // Caveat: charge termination at 100% reads as "not connected".
  static const BatteryMonitor battery;
  return battery.isCharging();
}

bool HalGPIO::coldBootImpliesPowerButton() const {
  // Xteink-style power topology: the power button energizes the rail until
  // firmware latches it, so a no-USB POWERON can only be a still-held button
  // boot, and plugging USB into an off device should charge-sleep, not boot.
  // Everything else boots on any cold boot: boards with no USB detection at
  // all (M5Paper v1.1, PaperColor, Murphy, de-link) would misread USB and
  // post-flash boots as battery button boots, and STAT-only boards like the
  // EEGO A4 misread them the same way once the charger terminates at 100%
  // (STAT inactive reads as "no USB").
  return isXteinkDevice() || BoardConfig::isPaperMono() || BoardConfig::isSticky();
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  const bool usbConnected = isUsbConnected();

  if (resetReason == ESP_RST_DEEPSLEEP &&
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1)) {
    return WakeupReason::PowerButton;
  }
#ifdef TENOR_PRESS_PROBE
  // CMD:WAKE_TIMER: a timed wake stands in for the power button, so wake can be measured over the cable.
  if (resetReason == ESP_RST_DEEPSLEEP && wakeupCause == ESP_SLEEP_WAKEUP_TIMER) return WakeupReason::PowerButton;
#endif
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected &&
      coldBootImpliesPowerButton()) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
