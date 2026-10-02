#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#include <BoardConfig.h>
#include <HalClock.h>
#include <HalTiltSensor.h>
#include <SdCardFontRegistry.h>
#include "SettingsList.h"
#include "activities/util/KeyboardLayoutSet.h"

// The settings constructor is defined in src/CrossPointSettings.cpp, outside this slice;
// it only lays the tenor/cross setup over the member initializers, as this one does.
CrossPointSettings::CrossPointSettings() { applyTenorPreset(); }

static bool measuring = false;
static size_t allocs = 0, bytes = 0, largest = 0;
void* operator new(size_t n) {
  if (measuring) { ++allocs; bytes += n; largest = std::max(largest, n); }
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
HalTiltSensor halTiltSensor;
HalClock halClock;
struct GpioBoundary { bool x3 = false; bool deviceIsX3() const { return x3; } } gpio;
struct FontBoundary {
  SdCardFontRegistry fonts;
  unsigned refreshes = 0;
  void refreshIfDirty() { ++refreshes; }
  const SdCardFontRegistry& registry() const { return fonts; }
} sdFontSystem;
// The Device tab's read-only panel chip row reads the boot's probe and NVS on the device.
namespace panelchip {
const std::string& current() {
  static const std::string value = "UC8279";
  return value;
}
}  // namespace panelchip
static std::vector<DictionaryEntry> discovered;
namespace DictionaryRegistry {
void discover(std::vector<DictionaryEntry>& out) { out = discovered; }
}
const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string& name) const {
  for (const auto& f : getFamilies()) if (f.name == name) return &f;
  return nullptr;
}
std::vector<uint8_t> SdCardFontFamilyInfo::availableSizes() const {
  std::vector<uint8_t> result;
  for (const auto& f : files) result.push_back(f.pointSize);
  return result;
}
void KOReaderCredentialStore::toJson(JsonDocument&) const {}
bool PersistableStoreBase::writeDocToFile(const char*, const JsonDocument&) { return true; }
void KOReaderCredentialStore::setCredentials(const std::string&, const std::string&) {}
void KOReaderCredentialStore::setServerUrl(const std::string&) {}
void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod) {}
void KOReaderCredentialStore::setSendMetadata(bool) {}
void KOReaderCredentialStore::setSyncBehavior(KOReaderSyncBehavior) {}

struct SettingsActivity {
  // Same shape as freeink::ui::ListNav: the selection is atomic (read by the render task).
  struct Cursor { std::atomic<int> selected{0}; bool followOnBuild = false; };
  struct InputBoundary { bool hasTouch() const { return BoardConfig::hasTouch(); } } mappedInput;
  int selectedCategoryIndex = 0, settingsCount = 0;
  std::vector<SettingInfo> displaySettings, readerSettings, controlsSettings, systemSettings,
                           deviceSettings, otherSettings, keyboardSettings, sleepSettings, motionSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;
  std::array<Cursor, settingstabs::TAB_COUNT> tabNavs{};
  unsigned rebuilds = 0;
  void rebuildRowItems() { ++rebuilds; }
  std::vector<SettingInfo>& danhSachCuaThe(settingstabs::Tab tab);
  void rebuildSettingsLists();
};
#include "CategoryMethods.inc"

static bool check(bool value, const char* message) {
  if (!value) std::fprintf(stderr, "FAIL %s\n", message);
  return value;
}
static void fonts(SdCardFontRegistry& registry, size_t count) {
  auto& rows = const_cast<std::vector<SdCardFontFamilyInfo>&>(registry.getFamilies());
  rows.clear();
  for (size_t i = 0; i < count; ++i) {
    SdCardFontFamilyInfo f;
    f.name = "SD family owned name index " + std::to_string(i);
    f.files = {{10, 0, 0}, {14, 0, 0}, {18, 0, 0}};
    rows.push_back(std::move(f));
  }
}
static bool dynamicLifetime() {
  SettingInfo family, size, dictionary;
  std::string fontName, dictionaryName;
  {
    SdCardFontRegistry temporary;
    fonts(temporary, 128);
    fontName = temporary.getFamilies()[127].name;
    std::strncpy(SETTINGS.sdFontFamilyName, fontName.c_str(), sizeof(SETTINGS.sdFontFamilyName)-1);
    family = buildFontFamilySetting(&temporary);
    size = buildFontSizeSetting(&temporary);
    std::vector<DictionaryEntry> temporaryDictionaries;
    for (int i = 0; i < 48; ++i)
      temporaryDictionaries.push_back({std::to_string(i) + " Dictionary very long owned label", "book"});
    dictionaryName = temporaryDictionaries.back().name;
    dictionary = buildDictionarySetting(temporaryDictionaries);
  }
  // Allocate different long strings after destruction to encourage stale aliases
  // to point at overwritten storage. ASan additionally checks heap lifetime.
  std::vector<std::string> churn(4096, std::string(97, 'X'));
  bool ok = true;
  family.valueSetter(129);
  ok &= check(family.valueGetter() == 129, "font getter owns registry names after destruction");
  ok &= check(family.enumStringValues[129] == fontName, "font render label owns memory");
  ok &= check(std::string(SETTINGS.sdFontFamilyName) == fontName, "font setter owns registry names");
  size.valueSetter(2);
  ok &= check(SETTINGS.fontPointSize == 18 && size.valueGetter() == 2, "font size callbacks own options");
  ok &= check(size.enumStringValues[2] == "18 pt", "font size render label survives child lifetime");
  dictionary.valueSetter(48);
  ok &= check(dictionary.valueGetter() == 48, "dictionary callbacks own names");
  ok &= check(dictionary.enumStringValues[48] == dictionaryName, "dictionary render label owns memory");
  ok &= check(std::string(SETTINGS.dictionaryName) == dictionaryName.substr(0, sizeof(SETTINGS.dictionaryName)-1), "dictionary setter preserved name");
  family.valueSetter(1);
  ok &= check(family.valueGetter() == 1 && SETTINGS.sdFontFamilyName[0] == '\0', "font live selection resets SD state");
  dictionary.valueSetter(0);
  ok &= check(dictionary.valueGetter() == 0 && SETTINGS.dictionaryName[0] == '\0', "dictionary live None selection");
  return ok;
}

int main(int argc, char** argv) {
  const bool enforce = argc > 1 && std::string(argv[1]) == "--enforce";
  const std::string board = argc > 2 ? argv[2] : "x3";
  const bool tenor = argc > 3 ? std::atoi(argv[3]) : true;
  const bool rtc = argc > 4 ? std::atoi(argv[4]) : true;
  const bool footnotes = argc > 5 ? std::atoi(argv[5]) : true;
  const bool hasDictionaries = argc > 6 ? std::atoi(argv[6]) : true;
  if (board == "x3") BoardConfig::ACTIVE = BoardConfig::XTEINK_X3;
  else if (board == "x4") BoardConfig::ACTIVE = BoardConfig::XTEINK_X4;
  else BoardConfig::ACTIVE = BoardConfig::XTEINK_X4_PRO;
  gpio.x3 = board == "x3";
  halTiltSensor.available = gpio.x3;
  halClock.available = rtc;
  SETTINGS.uiTheme = tenor ? CrossPointSettings::TENOR_UI : CrossPointSettings::CLASSIC;
  SETTINGS.shortPwrBtn = footnotes ? CrossPointSettings::FOOTNOTES : CrossPointSettings::IGNORE;
  bool ok = true;
  fonts(sdFontSystem.fonts, 128);
  if (hasDictionaries)
    for (int i = 0; i < 8; ++i)
      discovered.push_back({std::to_string(i) + " Dictionary long owned label", "book"});
  const auto& catalog = getBaseSettingsList();
  ok &= check(!catalog.empty() && catalog.front().valuePtr == &CrossPointSettings::uiTextSize,
              "UI text size is the first catalog row");
  SettingsActivity activity;
  for (auto& cursor : activity.tabNavs) cursor.selected = 10000;
  measuring = true;
  activity.rebuildSettingsLists();
  measuring = false;
  const size_t firstAllocs = allocs, firstBytes = bytes, firstLargest = largest;
  ok &= check(sdFontSystem.refreshes == 1 && activity.rebuilds == 1, "actual rebuild called discovery boundary and row renderer");
  size_t total = 0, capacity = 0;
  std::printf("{\"board\":\"%s\",\"tenor\":%d,\"rtc\":%d,\"footnotes\":%d,\"dictionaries\":%d,\"sizeof\":%zu,\"catalog\":%zu,\"allocs\":%zu,\"bytes\":%zu,\"largest\":%zu,\"tabs\":[",
              board.c_str(), tenor, rtc, footnotes, hasDictionaries, sizeof(SettingInfo), catalog.size(), firstAllocs, firstBytes, firstLargest);
  for (int tab = 0; tab < settingstabs::TAB_COUNT; ++tab) {
    const auto& rows = activity.danhSachCuaThe(static_cast<settingstabs::Tab>(tab));
    total += rows.size(); capacity += rows.capacity();
    if (enforce) ok &= check(rows.capacity() == rows.size(), "fresh category reserves exact row count");
    ok &= check(activity.tabNavs[tab].selected == static_cast<int>(rows.size()), "cursor clamps after rebuild");
    ok &= check(activity.tabNavs[tab].followOnBuild, "all tabs refresh navigation bounds");
    std::printf("%s{\"size\":%zu,\"capacity\":%zu,\"rows\":[", tab ? "," : "", rows.size(), rows.capacity());
    for (size_t i = 0; i < rows.size(); ++i) {
      const auto& row = rows[i];
      ok &= check(!row.inTextSettings, "text-only rows excluded from categories");
      std::printf("%s\"%d:%d:%s\"", i ? "," : "", static_cast<int>(row.nameId), static_cast<int>(row.action), row.key ? row.key : "");
    }
    std::printf("]}");
  }
  std::printf("],\"retained_rows\":%zu,\"retained_capacity\":%zu}", total, capacity);
  if (enforce) ok &= check(firstLargest < catalog.size()*sizeof(SettingInfo), "no full-catalog allocation while building category UI");
  const auto& readers = activity.readerSettings;
  ok &= check(readers[0].action == SettingAction::TextSettings && readers[1].action == SettingAction::DownloadFonts,
              "reader text and font actions preserve leading order");
  if (hasDictionaries) {
    // Every theme ends the reader tab with the status bar action, right after the dictionary.
    const auto dictionaryIndex = readers.size() - 2;
    ok &= check(readers[dictionaryIndex].nameId == StrId::STR_DICTIONARY, "dictionary is last reader descriptor before optional action");
    discovered.clear();
    readers[dictionaryIndex].valueSetter(8);
    ok &= check(readers[dictionaryIndex].valueGetter() == 8, "category dictionary survives discovery lifetime");
  }
  const auto clockAction = std::find_if(activity.systemSettings.begin(), activity.systemSettings.end(), [](const auto& row) {
    return row.action == SettingAction::ClockSettings;
  });
  ok &= check(clockAction != activity.systemSettings.end(), "clock action remains available without RTC hardware");
  if (tenor) {
    const auto labels = std::find_if(activity.displaySettings.begin(), activity.displaySettings.end(), [](const auto& row) {
      return row.valuePtr == &CrossPointSettings::tenorButtonSymbols;
    });
    ok &= check(labels != activity.displaySettings.end() && labels+1 != activity.displaySettings.end() &&
                (labels+1)->nameId == StrId::STR_STATUS_CORNERS, "clock adapter immediately follows labels");
  }
  allocs = bytes = largest = 0;
  measuring = true;
  activity.rebuildSettingsLists();
  measuring = false;
  if (enforce) ok &= check(largest < catalog.size()*sizeof(SettingInfo), "repeat rebuild has no full-catalog allocation");
  ok &= dynamicLifetime();
  std::printf("\n");
  return ok ? 0 : 1;
}
