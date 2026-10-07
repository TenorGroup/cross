#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>
#include <BoardConfig.h>
#include <HalClock.h>
#include <HalTiltSensor.h>
#include <SdCardFontRegistry.h>
#include <FreeInkUI.h>
#include <ObfuscationUtils.h>
#include "SettingsList.h"
#include "activities/util/KeyboardLayoutSet.h"

namespace mem {
struct alignas(std::max_align_t) Header { size_t bytes; unsigned epoch; };
unsigned epoch = 0;
bool active = false;
size_t live = 0, peak = 0;
bool requireReleaseLock = false;
unsigned renderLockDepth = 0;
}
void* operator new(size_t n) {
  auto* h = static_cast<mem::Header*>(std::malloc(sizeof(mem::Header) + (n ? n : 1)));
  if (!h) throw std::bad_alloc();
  h->bytes = n; h->epoch = mem::active ? mem::epoch : 0;
  if (mem::active) { mem::live += n; mem::peak = std::max(mem::peak, mem::live); }
  return h + 1;
}
void operator delete(void* p) noexcept {
  if (!p) return;
  auto* h = static_cast<mem::Header*>(p) - 1;
  if (h->epoch && h->epoch == mem::epoch) {
    if (mem::requireReleaseLock && mem::renderLockDepth == 0) std::abort();
    mem::live -= h->bytes;
  }
  std::free(h);
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }
#include "Boundaries.inc"
namespace fui = freeink::ui;
namespace shell {
bool isUgly() { return false; }
}
struct ActivityResult { bool isCancelled = false; };
struct FontDownloadActivity {
  template<class A, class B> FontDownloadActivity(A&, B&) {}
  FontDownloadActivity() = default;
  enum State { LOADING_MANIFEST, ERROR, GROUP_LIST, FAMILY_LIST } state_ = FAMILY_LIST;
  struct Nav { void reset() {} } groupNav_, nav;
  bool rowsDirty_ = false, finished = false, locked = false, sawReleased = false, fetchOk = true;
  void finish() { finished = true; }
  void requestUpdateAndWait() {}
  bool fetchAndParseManifest() { sawReleased = mem::live == 0; return fetchOk; }
  void clearManifest() {}
  bool hasGroupScreen() const { return false; }
  void buildFilteredIndices(int) {}
  void onWifiSelectionComplete(bool);
};
struct RenderLock {
  FontDownloadActivity* owner = nullptr;
  explicit RenderLock(FontDownloadActivity& value) : owner(&value) {
    if(owner->locked) std::abort();
    owner->locked = true; ++mem::renderLockDepth;
  }
  template<class T> explicit RenderLock(T&) { ++mem::renderLockDepth; }
  ~RenderLock() { if (owner) owner->locked = false; --mem::renderLockDepth; }
};
namespace obfuscation {
String obfuscateToBase64(const std::string& s) { return String(s.c_str()); }
std::string deobfuscateFromBase64(const char* s, size_t, bool* ok, bool* tooLong) {
  if (ok) *ok = true;
  if (tooLong) *tooLong = false;
  return s ? s : "";
}
}

struct SettingsActivity {
  // Same shape as freeink::ui::ListNav: the selection is atomic (read by the render task).
  struct Cursor { std::atomic<int> selected{0}; bool followOnBuild = false; };
  struct Input { bool hasTouch() const { return BoardConfig::hasTouch(); } } mappedInput;
  int renderer = 0;
  int selectedCategoryIndex = static_cast<int>(settingstabs::Tab::READER), settingsCount = 0;
  struct FormBoundary { void invalidate() {} } form_;
  std::atomic<bool> formPaintReady_{false};
  uint8_t formCount_ = 0;
  void bindForm() {}
  bool releaseListsForFontDownload_ = false;
  bool routingClosed = false;
  int saves = 0;
  std::vector<SettingInfo> displaySettings, readerSettings, controlsSettings, systemSettings,
                           deviceSettings, otherSettings, keyboardSettings, sleepSettings, motionSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;
  std::array<Cursor, settingstabs::TAB_COUNT> tabNavs{};
  std::vector<std::string> rowValues_;
  std::vector<fui::ListItem> rowItems_;
  std::function<void(const ActivityResult&)> resultHandler;
  void closeRouting() { routingClosed = true; }
  bool saveSettings() { ++saves; return true; }
  void startActivityForResult(std::unique_ptr<FontDownloadActivity>, std::function<void(const ActivityResult&)> handler) {
    resultHandler = std::move(handler);
  }
  std::vector<SettingInfo>& danhSachCuaThe(settingstabs::Tab);
  static bool listedAsRow(const SettingInfo& setting);
  void rebuildSettingsLists(bool lockHeld = false);
  void rebuildRowItems();
  void onPause();
  void onResume();
  void launchFontDownload();
};
// The X4 Pro About & updates screen (InfoUpdateActivity) holds no rows on these boards.
namespace infoupdate { inline bool holds(settingstabs::Action, bool = false) { return false; } }
#include "Methods.inc"
#include "PostWifiMethods.inc"

bool check(bool value, const char* message) {
  if (!value) std::printf("FAIL %s\n", message);
  return value;
}

std::string jsonSettings() {
  JsonDocument doc;
  SETTINGS.toJson(doc);
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool catalogReleaseProbe() {
  ++mem::epoch; mem::live = mem::peak = 0; mem::active = true;
  const auto& initial = getBaseSettingsList();
  mem::active = false;
  const size_t bytes = mem::live;
  const auto copied = initial;
  const auto webCopy = getSettingsList();
  const auto clock = std::find_if(copied.begin(), copied.end(), [](const auto& row) {
    return row.valuePtr == &CrossPointSettings::statusBarClock;
  });
  const auto clockCopy = buildTenorClockPlacementSetting(*clock);
  const std::string beforeJson = jsonSettings();
  releaseBaseSettingsList();
  bool ok = check(mem::live == 0, "shared catalog allocations released");
  std::printf("catalog_rows=%zu sizeof_SettingInfo=%zu allocated=%zu after_release=%zu\n",
              copied.size(), sizeof(SettingInfo), bytes, mem::live);
  ok &= check(copied.size() == 100 && !webCopy.empty(), "100 base rows and owned web copy survive release");
  clockCopy.valueSetter(1);
  ok &= check(clockCopy.valueGetter() == 1, "copied dynamic callback survives catalog release");
  SETTINGS.statusBarClock = CrossPointSettings::STATUS_BAR_CLOCK_HIDE;
  // Compare every field that shapes ordering, JSON persistence and enum rendering.
  mem::active = true;
  const auto& rebuilt = getBaseSettingsList();
  mem::active = false;
  ok &= check(rebuilt.size() == copied.size() && rebuilt.capacity() == copied.size(), "exact rebuild capacity/count");
  for (size_t i = 0; i < std::min(rebuilt.size(), copied.size()); ++i) {
    const auto& a = copied[i]; const auto& b = rebuilt[i];
    ok &= check(a.nameId == b.nameId && a.type == b.type && a.valuePtr == b.valuePtr &&
                a.enumValues == b.enumValues && a.enumStringValues == b.enumStringValues && a.action == b.action &&
                a.valueRange.min == b.valueRange.min && a.valueRange.max == b.valueRange.max && a.valueRange.step == b.valueRange.step &&
                a.key == b.key && a.category == b.category && a.obfuscated == b.obfuscated &&
                a.inTextSettings == b.inTextSettings && a.stringOffset == b.stringOffset && a.stringMaxLen == b.stringMaxLen,
                "descriptor order/keys/enums/persistence fields unchanged");
  }
  JsonDocument prior;
  deserializeJson(prior, beforeJson);
  ok &= check(SETTINGS.fromJson(prior.as<JsonVariantConst>()), "real settings JSON reload succeeds");
  ok &= check(jsonSettings() == beforeJson, "real settings JSON byte-equivalent roundtrip");
  // A Wi-Fi time save builds no catalog (forEachBaseSetting, v1.0.53). Release must still happen after that
  // child returns and before manifest TLS, using the actual production callback.
  releaseBaseSettingsList();
  JsonDocument wifiSave;
  mem::active = true;
  SETTINGS.toJson(wifiSave);
  mem::active = false;
  ok &= check(mem::live == 0, "Wi-Fi save builds no catalog");
  FontDownloadActivity font;
  mem::requireReleaseLock = true;
  font.onWifiSelectionComplete(true);
  mem::requireReleaseLock = false;
  ok &= check(font.sawReleased && !font.locked, "catalog released after Wi-Fi before manifest TLS");
  FontDownloadActivity cancelled;
  cancelled.onWifiSelectionComplete(false);
  ok &= check(cancelled.finished, "Wi-Fi cancellation still finishes child");
  // Fail manifest and rebuild settings on return in the lifecycle test below.
  FontDownloadActivity failed;
  failed.fetchOk = false;
  failed.onWifiSelectionComplete(true);
  ok &= check(failed.state_ == FontDownloadActivity::ERROR && failed.sawReleased, "manifest failure preserves release and error path");
  ok &= check(jsonSettings() == beforeJson, "persistence getter rebuilds safely after release/error");
  return ok;
}

int main() {
  BoardConfig::ACTIVE = BoardConfig::XTEINK_X3;
  gpio.x3 = true; halTiltSensor.available = true;
  (void)I18N.get(StrId::STR_SETTINGS_TITLE);
  bool ok = catalogReleaseProbe();
  for (const bool dictionaries : {false, true}) {
    discovered.clear();
    if (dictionaries) discovered.push_back({"Long dictionary owned label for lifetime verification", "book"});
    (void)getBaseSettingsList();  // resident before the screen, as a boot that read settings left it until v1.0.53
    SettingsActivity activity;
    ++mem::epoch; mem::live = mem::peak = 0; mem::active = true;
    activity.rebuildSettingsLists();
    mem::active = false;
    const size_t before = mem::live;
    size_t categories = 0;
    for (int t = 0; t < settingstabs::TAB_COUNT; ++t) categories += activity.danhSachCuaThe(static_cast<settingstabs::Tab>(t)).size();
    activity.tabNavs[static_cast<int>(settingstabs::Tab::READER)].selected = 2;
    activity.onPause();
    ok &= check(mem::live == before && activity.currentSettings && activity.settingsCount > 0,
                "unrelated child preserves live settings and descriptor storage");
    activity.onResume();
    activity.launchFontDownload();
    ok &= check(mem::live == before && activity.settingsCount > 0, "launch defers release until pause");
    activity.onPause();
    const size_t after = mem::live;
    std::printf("dictionary=%d rows=%zu sizeof_SettingInfo=%zu owned_before=%zu owned_paused=%zu released=%zu\n",
                dictionaries, categories, sizeof(SettingInfo), before, after, before-after);
    ok &= check(after == 0, "all settings-owned allocations returned while font download is active");
    ok &= check(activity.currentSettings == nullptr && activity.settingsCount == 0 && activity.routingClosed,
                "paused screen has no stale row access");
    for (int t = 0; t < settingstabs::TAB_COUNT; ++t)
      ok &= check(activity.danhSachCuaThe(static_cast<settingstabs::Tab>(t)).capacity() == 0, "category capacity released");
    ok &= check(activity.rowItems_.capacity() == 0 && activity.rowValues_.capacity() == 0, "row capacities released");
    // Emulate child return, including the early BLE-acquire failure/cancel route.
    releaseBaseSettingsList();
    activity.onResume();
    ok &= check(activity.currentSettings && activity.settingsCount > 0, "rows rebuilt before result callback");
    ok &= check(activity.readerSettings[1].action == SettingAction::DownloadFonts,
                "font action restored at same row");
    ok &= check(activity.tabNavs[static_cast<int>(settingstabs::Tab::READER)].selected == 2,
                "selected navigation row preserved");
    ok &= check(activity.rowItems_.size() == static_cast<size_t>(activity.settingsCount), "render rows restored");
    activity.resultHandler(ActivityResult{true});
    ok &= check(activity.saves == 1, "cancel/return still saves settings");
    if (dictionaries) {
      const auto it = std::find_if(activity.readerSettings.begin(), activity.readerSettings.end(),
          [](const auto& row) { return row.nameId == StrId::STR_DICTIONARY; });
      ok &= check(it != activity.readerSettings.end(), "dictionary descriptor restored");
      it->valueSetter(1);
      ok &= check(it->valueGetter() == 1, "restored dynamic callback remains usable");
    }
    const auto* storage = activity.readerSettings.data();
    activity.onPause(); activity.onResume();
    ok &= check(storage == activity.readerSettings.data(), "font release flag cleared after return");
  }
  std::printf("settings_font_memory=%s\n", ok ? "GREEN" : "RED");
  return ok ? 0 : 1;
}
