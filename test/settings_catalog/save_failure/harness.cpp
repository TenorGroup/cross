#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <I18n.h>
#include "CrossPointSettings.h"
#include "activities/settings/SettingsActivity.h"
#include "components/SettledListRender.h"

static bool persistOk = true;
static unsigned writes = 0, errors = 0;
void CrossPointSettings::toJson(JsonDocument& doc) const { doc["test"] = deviceName; }
// The settings constructor is defined in src/CrossPointSettings.cpp, outside this slice;
// it only lays the tenor/cross setup over the member initializers, as this one does.
CrossPointSettings::CrossPointSettings() { applyTenorPreset(); }
bool PersistableStoreBase::writeDocToFile(const char*, const JsonDocument&) {
  ++writes;
  return persistOk;
}
#undef LOG_ERR
#define LOG_ERR(...) (++errors)

struct RenderLock {};
struct GfxRenderer {
  unsigned clears = 0, displayed = 0, popups = 0, popupAtDisplay = 0;
  void clearScreen() { ++clears; popups = 0; }
  int getScreenWidth() const { return 528; }
  void displayBuffer() { ++displayed; popupAtDisplay = popups; }
};
struct MappedInputManager {
  enum class Button { Confirm, Back };
  bool confirm = false, back = false;
  bool wasReleased(Button button) const { return button == Button::Confirm ? confirm : back; }
  struct Labels { const char *btn1, *btn2, *btn3, *btn4; };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) { return {a,b,c,d}; }
};
struct GuiBoundary {
  std::string lastPopup;
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) {}
  void drawPopup(GfxRenderer& renderer, const char* label) { ++renderer.popups; lastPopup = label; }
} GUI;
struct UITheme {
  static UITheme& getInstance() { static UITheme instance; return instance; }
  struct Metrics {};
  const Metrics& getMetrics() const { static Metrics metrics; return metrics; }
};
namespace tenorchrome {
bool enabled() { return false; }
void drawSiblingDestinations(GfxRenderer&, const char*, const char*) {}
void drawTip(GfxRenderer&, const char*, int, int = 4) {}
}
struct GpioBoundary { bool deviceIsX3() const { return true; } } gpio;
struct FontBoundary { const int& registry() { static int registry; return registry; } } sdFontSystem;
struct ManagerBoundary { void goToFileTransfer() {} void goToBrowser() {} } activityManager;
struct KeyboardResult { std::string text; };
struct IntervalResult { int value = 1; };
struct ActivityResult { bool isCancelled = false; std::variant<KeyboardResult, IntervalResult> data; };
enum class InputType { Text };
struct ChildBoundary { template<class... Args> explicit ChildBoundary(Args&&...) {} };
#define CHILD(name) struct name : ChildBoundary { using ChildBoundary::ChildBoundary; }
CHILD(ButtonRemapActivity);
CHILD(StatusBarSettingsActivity);
CHILD(KOReaderSettingsActivity);
CHILD(OpdsServerListActivity);
CHILD(BlePageTurnerActivity);
CHILD(KeyboardEntryActivity);
CHILD(WifiSelectionActivity);
CHILD(ClearCacheActivity);
CHILD(OtaUpdateActivity);
CHILD(SdFirmwareUpdateActivity);
CHILD(FontDownloadActivity);
CHILD(LanguageSelectActivity);
CHILD(KeyboardLayoutsActivity);
CHILD(ClockSettingsActivity);
CHILD(HomeButtonSettingsActivity);
CHILD(AboutActivity);
CHILD(IntervalSelectionActivity);
struct TextSettingsActivity : ChildBoundary {
  using ChildBoundary::ChildBoundary;
  enum class Tab { Family };
};
template<class T, class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct PopupBoundary {
  bool active = false;
  std::function<void(int)> callback;
  bool processRender(GfxRenderer&, MappedInputManager&) { return active; }
  void show(StrId, const StrId*, int, int, std::function<void(int)> callback) {
    active = true; this->callback = std::move(callback);
  }
  void show(StrId, const std::vector<std::string>&, int, std::function<void(int)> callback) {
    active = true; this->callback = std::move(callback);
  }
  void choose(int i) { active = false; auto cb = std::move(callback); cb(i); }
};
struct ListNav {
  bool consumeRebuildNeeded() { return false; }
};
struct SettingsActivity {
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  PopupBoundary optionPopup;
#include "State.inc"
  bool fromHomeGroup = false;
  bool releaseListsForFontDownload_ = false;
  unsigned finished = 0, home = 0, updates = 0, rebuilds = 0;
  bool uiApplyOk = true;
  uint8_t uiSizeSeenDuringApply = 255;
  int selectedCategoryIndex = 0, settingsCount = 1;
  ListNav nav;
  std::vector<SettingInfo> rows;
  const std::vector<SettingInfo>* currentSettings = &rows;
  std::function<void(const ActivityResult&)> childCallback;
  void requestUpdate() { ++updates; }
  void finish() { ++finished; }
  void onGoHome() { ++home; }
  void nhanNhom() {}
  int ringPos() const { return 1; }
  int activeTab() const { return 0; }
  ListNav& activeNav() { return nav; }
  int tabCount() const { return settingstabs::TAB_COUNT; }
  int adjacentTab(int direction) const { return direction; }
  const char* tabLabel(int) const { return "Settings"; }
  void drawNavigationHeader(const char*) {}
  void renderUi() {}
  void rebuildSettingsLists() { ++rebuilds; }
  bool applyUiSettingChange(uint8_t CrossPointSettings::*) { return true; }
  bool applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr, uint8_t value) {
    if (valuePtr != &CrossPointSettings::uiTextSize) return true;
    uiSizeSeenDuringApply = SETTINGS.uiTextSize;
    if (!uiApplyOk) return false;
    SETTINGS.uiTextSize = value;
    return true;
  }
  void syncQuickResumeTimeoutForSleepScreen(bool, bool) {}
  template<class T> void startActivityForResult(std::unique_ptr<T>, std::function<void(const ActivityResult&)> cb) {
    childCallback = std::move(cb);
  }
  bool saveSettings();
  bool handleButtons();
  void toggleCurrentSetting();
  void openSleepTimeoutPicker();
  bool showWakeHint() const;
  void render(RenderLock&&);
};
#include "Methods.inc"

static bool check(bool value, const char* message) {
  if (!value) std::fprintf(stderr, "FAIL %s\n", message);
  return value;
}
static void row(SettingsActivity& activity, SettingInfo value) {
  activity.rows.clear(); activity.rows.push_back(std::move(value));
}
static bool failureShown(SettingsActivity& activity, const char* where) {
  activity.render(RenderLock{});
  const bool shown = activity.renderer.popupAtDisplay == 1 && GUI.lastPopup == tr(STR_HABIT_SAVE_FAILED);
  return check(shown, where) && check(activity.updates > 0, "failure schedules repaint");
}
static bool retryAndExit(SettingsActivity& activity, bool fromHome) {
  activity.fromHomeGroup = fromHome;
  activity.mappedInput.confirm = false;
  activity.mappedInput.back = true;
  persistOk = false;
  bool ok = check(activity.handleButtons(), "Back consumed while save fails");
  ok &= check(activity.finished == 0 && activity.home == 0, "failed Back keeps settings open");
  ok &= failureShown(activity, "Back failure visible at displayBuffer");
  persistOk = true;
  ok &= check(activity.handleButtons(), "retry Back consumed");
  ok &= check(activity.finished == (fromHome ? 1u : 0u) && activity.home == (fromHome ? 0u : 1u),
              "successful retry follows original navigation route");
  activity.render(RenderLock{});
  ok &= check(activity.renderer.popupAtDisplay == 0, "successful retry clears error popup");
  return ok;
}
int main() {
  bool ok = true;
  unsigned scenarios = 0;
  for (bool fromHome : {false,true}) {
    SettingsActivity activity;
    row(activity, SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles));
    ok &= retryAndExit(activity, fromHome); ++scenarios;
  }
  {
    SettingsActivity activity;
    row(activity, SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles));
    const uint8_t old = SETTINGS.showHiddenFiles;
    persistOk = false; activity.mappedInput.confirm = true;
    ok &= check(activity.handleButtons(), "Confirm consumed");
    ok &= check(SETTINGS.showHiddenFiles != old, "toggle executes before save attempt");
    ok &= failureShown(activity, "direct toggle failure visible");
    ++scenarios;
  }
  {
    SettingsActivity activity;
    row(activity, SettingInfo::Enum(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles,
         {StrId::STR_STATE_OFF,StrId::STR_STATE_ON,StrId::STR_SELECT,StrId::STR_BACK}));
    persistOk = false; activity.toggleCurrentSetting();
    ok &= check(activity.optionPopup.active, "enum opens picker");
    activity.optionPopup.choose(2);
    ok &= check(SETTINGS.showHiddenFiles == 2, "enum callback sets selected value");
    ok &= failureShown(activity, "enum callback save failure visible");
    ++scenarios;
  }
  {
    SettingsActivity activity;
    SETTINGS.uiTextSize = 0;
    row(activity, SettingInfo::Enum(StrId::STR_UI_TEXT_SIZE, &CrossPointSettings::uiTextSize,
         {StrId::STR_UI_SIZE_SMALL,StrId::STR_UI_SIZE_MEDIUM,StrId::STR_UI_SIZE_LARGE}));
    persistOk = true;
    activity.toggleCurrentSetting();
    ok &= check(activity.uiSizeSeenDuringApply == 0, "direct UI apply sees the previous setting");
    ok &= check(SETTINGS.uiTextSize == 1, "direct UI apply publishes candidate before save");
    ++scenarios;
  }
  {
    SettingsActivity activity;
    SETTINGS.uiTextSize = 0;
    activity.uiApplyOk = false;
    const unsigned writesBefore = writes;
    row(activity, SettingInfo::Enum(StrId::STR_UI_TEXT_SIZE, &CrossPointSettings::uiTextSize,
         {StrId::STR_UI_SIZE_SMALL,StrId::STR_UI_SIZE_MEDIUM,StrId::STR_UI_SIZE_LARGE}));
    activity.toggleCurrentSetting();
    ok &= check(activity.uiSizeSeenDuringApply == 0, "failed UI apply sees the previous setting");
    ok &= check(SETTINGS.uiTextSize == 0, "failed UI apply keeps the previous setting");
    ok &= check(writes == writesBefore, "failed UI apply is not persisted");
    ++scenarios;
  }
  {
    SettingsActivity activity;
    SETTINGS.uiTextSize = 1;
    row(activity, SettingInfo::Enum(StrId::STR_UI_TEXT_SIZE, &CrossPointSettings::uiTextSize,
         {StrId::STR_UI_SIZE_SMALL,StrId::STR_UI_SIZE_MEDIUM,StrId::STR_UI_SIZE_LARGE,StrId::STR_SELECT}));
    persistOk = true;
    activity.toggleCurrentSetting();
    ok &= check(activity.optionPopup.active, "four-value UI enum opens picker");
    activity.optionPopup.choose(3);
    ok &= check(activity.uiSizeSeenDuringApply == 1, "popup UI apply sees the previous setting");
    ok &= check(SETTINGS.uiTextSize == 3, "popup UI apply publishes candidate before save");
    ++scenarios;
  }
  for (bool strings : {false,true}) {
    SettingsActivity activity;
    uint8_t selected = 0;
    auto setting = SettingInfo::DynamicEnum(StrId::STR_DICTIONARY,
      {StrId::STR_STATE_OFF,StrId::STR_STATE_ON,StrId::STR_SELECT,StrId::STR_BACK},
      [&selected] { return selected; }, [&selected](uint8_t value) { selected = value; });
    if (strings) setting.enumStringValues = {"None", "One", "Two", "Three"};
    row(activity,std::move(setting));
    persistOk = false; activity.toggleCurrentSetting();
    activity.optionPopup.choose(3);
    ok &= check(selected == 3, "dynamic picker callback sets selected value");
    ok &= failureShown(activity, "dynamic enum callback save failure visible");
    ++scenarios;
  }
  for (SettingAction action : {SettingAction::RemapFrontButtons, SettingAction::DeviceName,
                              SettingAction::DownloadFonts, SettingAction::Language}) {
    SettingsActivity activity;
    row(activity, SettingInfo::Action(StrId::STR_DEVICE_NAME, action));
    persistOk = false; activity.toggleCurrentSetting();
    ok &= check(static_cast<bool>(activity.childCallback), "child activity registers callback");
    ActivityResult result;
    result.data = KeyboardResult{"Changed name"};
    activity.childCallback(result);
    ok &= failureShown(activity, "child callback save failure visible");
    ++scenarios;
  }
  {
    SettingsActivity activity;
    row(activity, SettingInfo::Value(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,{1,31,1}));
    persistOk = false; activity.toggleCurrentSetting();
    ok &= check(static_cast<bool>(activity.childCallback), "timeout picker callback registered");
    ActivityResult result; result.data = IntervalResult{7}; activity.childCallback(result);
    ok &= check(SETTINGS.sleepTimeoutMinutes == 7, "timeout callback sets selected value");
    ok &= failureShown(activity, "sleep timeout callback save failure visible");
    ++scenarios;
  }
  std::printf("%u scenarios; writes=%u; logged_errors=%u; %s\n", scenarios,writes,errors,ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}
