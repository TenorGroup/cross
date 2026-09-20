#include "TestPlatform.h"

#define class struct
#define private public
#include "WifiSelectionActivity.h"
#undef private
#undef class

#include "WifiSelectionActivity.cpp"

#include <iostream>

namespace {
int failures = 0;

void expect(const bool condition, const char* name) {
  if (condition) {
    std::cout << "PASS " << name << '\n';
  } else {
    std::cout << "FAIL " << name << '\n';
    failures++;
  }
}

void resetPlatform() {
  testMillis = 0;
  WiFi.reset();
  WIFI_STORE = WifiCredentialStore{};
  powerManager = HalPowerManager{};
  halClock = HalClock{};
  SETTINGS = CrossPointSettings{};
  Activity::didFinish = false;
  Activity::didRequestUpdate = false;
  filetransfer::owners = 0;
  timezone_lookup::calls = 0;
  timezone_lookup::result = false;
  GUI.lastButton2.clear();
}

struct Fixture {
  GfxRenderer renderer;
  MappedInputManager input;
  WifiSelectionActivity activity;

  explicit Fixture(bool autoConnect = false, bool syncClock = true)
      : activity(renderer, input, autoConnect, syncClock, false) {
    activity.runtimeStarted = true;
  }
};

void scanDeadlineStopsRadio() {
  resetPlatform();
  Fixture f;
  f.activity.state = WifiSelectionState::SCANNING;
  f.activity.scanStartTime = 0;
  WiFi.currentMode = WIFI_STA;
  WiFi.scanState = WIFI_SCAN_RUNNING;
  testMillis = WifiSelectionActivity::SCAN_TIMEOUT_MS + 1;
  f.activity.loop();
  expect(f.activity.state == WifiSelectionState::NETWORK_LIST, "scan deadline enters list");
  expect(WiFi.currentMode == WIFI_MODE_NULL && powerManager.saving, "scan deadline stops radio and lowers clock");
}

void joinDeadlineStopsRadio() {
  resetPlatform();
  Fixture f;
  f.activity.state = WifiSelectionState::CONNECTING;
  f.activity.connectionStartTime = 0;
  WiFi.currentMode = WIFI_STA;
  WiFi.connectionStatus = WL_IDLE_STATUS;
  testMillis = WifiSelectionActivity::CONNECTION_TIMEOUT_MS + 1;
  f.activity.loop();
  expect(f.activity.state == WifiSelectionState::CONNECTION_FAILED, "join deadline enters failure");
  expect(WiFi.currentMode == WIFI_MODE_NULL && powerManager.saving, "join deadline stops radio");
}

void manualBackReturnsToFocusedList() {
  resetPlatform();
  Fixture f;
  f.activity.networks = {{"one", -40, true, false}, {"two", -50, true, false}};
  f.activity.selectedNetworkIndex = 1;
  f.activity.state = WifiSelectionState::CONNECTING;
  WiFi.currentMode = WIFI_STA;
  f.input.pressed = MappedInputManager::Button::Back;
  f.activity.loop();
  expect(f.activity.state == WifiSelectionState::NETWORK_LIST, "manual Back returns to list");
  expect(f.activity.selectedNetworkIndex == 1, "manual Back preserves focused SSID");
  expect(WiFi.currentMode == WIFI_MODE_NULL, "manual Back stops radio");
}

void completedScanLeavesListRadioOff() {
  resetPlatform();
  Fixture f;
  f.activity.state = WifiSelectionState::SCANNING;
  WiFi.currentMode = WIFI_STA;
  WiFi.found = {{"alpha", -40, 1}, {"beta", -50, 1}};
  WiFi.scanState = 2;
  f.activity.loop();
  expect(f.activity.state == WifiSelectionState::NETWORK_LIST, "completed scan enters list");
  expect(WiFi.currentMode == WIFI_MODE_NULL && powerManager.saving, "network list keeps radio off");
}

void savePromptTimeoutCancelsWithoutHandoff() {
  resetPlatform();
  Fixture f;
  f.activity.state = WifiSelectionState::SAVE_PROMPT;
  f.activity.savePromptStartTime = 0;
  WiFi.currentMode = WIFI_STA;
  testMillis = WifiSelectionActivity::SAVE_PROMPT_TIMEOUT_MS + 1;
  f.activity.loop();
  expect(Activity::didFinish && f.activity.finalResult.isCancelled, "save timeout returns cancelled result");
  expect(!f.activity.wifiConnectionHandedOff && WiFi.currentMode == WIFI_MODE_NULL,
         "save timeout cannot hand connection to parent");
}

void ntpUsesSystemValidityWithoutRtc() {
  resetPlatform();
  Fixture f(false, true);
  f.activity.state = WifiSelectionState::CONNECTING;
  f.activity.selectedSSID = "saved";
  f.activity.usedSavedPassword = true;
  WiFi.currentMode = WIFI_STA;
  WiFi.connectionStatus = WL_CONNECTED;
  SETTINGS.clockHasBeenSynced = 1;
  SETTINGS.clockAutoTimezone = 1;
  halClock.available = false;
  halClock.valid = false;
  f.activity.loop();
  expect(halClock.syncCalls == 1, "invalid epoch resyncs without RTC");
  expect(timezone_lookup::calls == 1, "valid X4 system time runs automatic timezone lookup");
  expect(f.activity.wifiConnectionHandedOff && WiFi.currentMode == WIFI_STA,
         "successful connection remains active for parent");
}

void invalidEpochSkipsTimezoneAfterFailedNtp() {
  resetPlatform();
  Fixture f(false, true);
  f.activity.state = WifiSelectionState::CONNECTING;
  f.activity.selectedSSID = "saved";
  f.activity.usedSavedPassword = true;
  WiFi.currentMode = WIFI_STA;
  WiFi.connectionStatus = WL_CONNECTED;
  SETTINGS.clockHasBeenSynced = 1;
  SETTINGS.clockAutoTimezone = 1;
  halClock.available = false;
  halClock.valid = false;
  halClock.syncResult = false;
  f.activity.loop();
  expect(halClock.syncCalls == 1, "cold X4 attempts NTP before timezone");
  expect(timezone_lookup::calls == 0, "invalid epoch skips automatic timezone lookup");
}

void exitPreservesOnlySuccessfulHandoff() {
  resetPlatform();
  Fixture success;
  success.activity.wifiConnectionHandedOff = true;
  WiFi.currentMode = WIFI_STA;
  success.activity.onExit();
  expect(WiFi.currentMode == WIFI_STA && WiFi.disconnectCalls == 0, "onExit preserves successful handoff");

  resetPlatform();
  Fixture cancelled;
  WiFi.currentMode = WIFI_STA;
  cancelled.activity.onExit();
  expect(WiFi.currentMode == WIFI_MODE_NULL && WiFi.disconnectCalls == 1, "onExit cleans unowned connection");
}

void rescanRestoresFocusBySsid() {
  resetPlatform();
  Fixture f;
  f.activity.state = WifiSelectionState::NETWORK_LIST;
  f.activity.networks = {{"alpha", -30, true, false}, {"beta", -40, true, false}, {"gamma", -50, true, false}};
  f.activity.selectedNetworkIndex = 1;
  f.activity.startWifiScan();
  WiFi.found = {{"gamma", -20, 1}, {"alpha", -30, 1}, {"beta", -60, 1}};
  WiFi.scanState = 3;
  f.activity.processWifiScanResults();
  expect(f.activity.networks.at(f.activity.selectedNetworkIndex).ssid == "beta", "rescan restores focus by SSID");
}

void failureBackAndConfirmHaveSeparateMeanings() {
  resetPlatform();
  Fixture f;
  f.activity.state = WifiSelectionState::CONNECTION_FAILED;
  f.activity.usedSavedPassword = true;
  f.activity.selectedSSID = "saved";
  f.input.pressed = MappedInputManager::Button::Back;
  f.activity.loop();
  expect(f.activity.state == WifiSelectionState::NETWORK_LIST, "failure Back returns to list");

  f.activity.state = WifiSelectionState::CONNECTION_FAILED;
  f.input.pressed = MappedInputManager::Button::Confirm;
  f.activity.loop();
  expect(f.activity.state == WifiSelectionState::FORGET_PROMPT && f.activity.forgetPromptSelection == 0,
         "failure Confirm opens saved-network forget prompt");
}

void wifiPasswordUsesMaskedCancelKeyboard() {
  resetPlatform();
  Fixture f;
  f.activity.promptHiddenSsid();
  auto* ssidKeyboard = dynamic_cast<KeyboardEntryActivity*>(f.activity.child.get());
  expect(ssidKeyboard && ssidKeyboard->capturedType == InputType::Text && ssidKeyboard->capturedBackCancels,
         "WiFi SSID keyboard opts into Back cancellation");

  f.activity.promptPasswordEntry();
  auto* keyboard = dynamic_cast<KeyboardEntryActivity*>(f.activity.child.get());
  expect(keyboard && keyboard->capturedType == InputType::Password, "WiFi password uses masked input");
  expect(keyboard && keyboard->capturedBackCancels, "WiFi keyboard opts into Back cancellation");
}

void autoFlowHintSaysChooseNetwork() {
  resetPlatform();
  Fixture f;
  f.activity.state = WifiSelectionState::AUTO_CONNECTING;
  f.activity.autoConnecting = true;
  const Rect screen{};
  const ThemeMetrics metrics{};
  f.activity.renderConnecting(&screen, &metrics);
  expect(GUI.lastButton2 == "Chon mang", "auto-flow Confirm hint says choose network");
}
}  // namespace

int main() {
  scanDeadlineStopsRadio();
  joinDeadlineStopsRadio();
  manualBackReturnsToFocusedList();
  completedScanLeavesListRadioOff();
  savePromptTimeoutCancelsWithoutHandoff();
  ntpUsesSystemValidityWithoutRtc();
  invalidEpochSkipsTimezoneAfterFailedNtp();
  exitPreservesOnlySuccessfulHandoff();
  rescanRestoresFocusBySsid();
  failureBackAndConfirmHaveSeparateMeanings();
  wifiPasswordUsesMaskedCancelKeyboard();
  autoFlowHintSaysChooseNetwork();
  std::cout << (failures ? "FAILURES " : "ALL PASS ") << failures << '\n';
  return failures == 0 ? 0 : 1;
}
