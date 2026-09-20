#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <BoardConfig.h>
#include <HalClock.h>
#include <HalTiltSensor.h>
#include <SdCardFontRegistry.h>
#include <FreeInkUI.h>
#include "SettingsList.h"
#include "activities/util/KeyboardLayoutSet.h"

namespace mem {
struct alignas(std::max_align_t) Header { size_t bytes; unsigned epoch; };
unsigned epoch = 0;
bool active = false;
size_t live = 0, peak = 0;
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
  if (h->epoch && h->epoch == mem::epoch) mem::live -= h->bytes;
  std::free(h);
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }
#include "Boundaries.inc"
namespace fui = freeink::ui;
struct ActivityResult { bool isCancelled = false; };
struct FontDownloadActivity { template<class A, class B> FontDownloadActivity(A&, B&) {} };

struct SettingsActivity {
  struct Cursor { int selected = 0; bool followOnBuild = false; };
  struct Input { bool hasTouch() const { return BoardConfig::hasTouch(); } } mappedInput;
  int renderer = 0;
  int selectedCategoryIndex = static_cast<int>(settingstabs::Tab::READER), settingsCount = 0;
  bool releaseListsForFontDownload_ = false;
  bool routingClosed = false;
  int saves = 0;
  std::vector<SettingInfo> displaySettings, readerSettings, controlsSettings, systemSettings,
                           deviceSettings, otherSettings, keyboardSettings;
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
  void rebuildSettingsLists();
  void rebuildRowItems();
  void onPause();
  void onResume();
  void launchFontDownload();
};
#include "Methods.inc"

bool check(bool value, const char* message) {
  if (!value) std::printf("FAIL %s\n", message);
  return value;
}

int main() {
  BoardConfig::ACTIVE = BoardConfig::XTEINK_X3;
  gpio.x3 = true; halTiltSensor.available = true;
  SETTINGS.uiTheme = CrossPointSettings::TENOR_UI;
  (void)getBaseSettingsList();
  (void)I18N.get(StrId::STR_SETTINGS_TITLE);
  bool ok = true;
  for (const bool dictionaries : {false, true}) {
    discovered.clear();
    if (dictionaries) discovered.push_back({"Long dictionary owned label for lifetime verification", "book"});
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
