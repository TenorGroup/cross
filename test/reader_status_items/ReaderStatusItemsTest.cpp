// Reader status bar items: chapter name, chapter page count and book percentage
// each have their own switch, while Off, battery and clock stay with the combined
// reader mode. A settings file written before the switches existed must keep
// showing exactly what its mode showed, whatever stale values the three switch
// keys carry (they were saved, and editable on the web page, while nothing read them).

#include <cstdio>

#include "SettingsList.h"

namespace {

using S = CrossPointSettings;

struct Seen {
  bool title, pages, percent, battery, clock;
};

// What each combined mode showed before the switches, one row per stored value.
constexpr Seen OLD_MODES[S::READER_STATUS_BAR_MODE_COUNT] = {
    {false, false, false, false, false},  // Off
    {false, false, false, true, true},    // Clock & battery
    {true, true, true, true, true},       // Full default
    {true, true, false, false, false},    // Chapter name & chapter progress
    {true, false, false, false, true},    // Chapter name & clock
    {true, false, false, true, false},    // Chapter name & battery
};

int failures = 0;

void expect(const bool ok, const char* what, const int mode) {
  if (ok) return;
  ++failures;
  std::printf("FAIL mode %d: %s\n", mode, what);
}

Seen seen() {
  const auto spec = SETTINGS.statusBarSpec();
  return {spec.showsTitle(), spec.showChapterPageCount, spec.showBookProgressPercent, spec.showBattery,
          spec.showsClock()};
}

bool same(const Seen& a, const Seen& b) {
  return a.title == b.title && a.pages == b.pages && a.percent == b.percent && a.battery == b.battery &&
         a.clock == b.clock;
}

// A file from before the switches: the mode, plus switch values that disagree with it.
void loadLegacy(const int mode) {
  JsonDocument doc;
  doc["readerStatusBarMode"] = mode;
  doc["statusBarTitle"] = OLD_MODES[mode].title ? S::HIDE_TITLE : S::CHAPTER_TITLE;
  doc["statusBarChapterPageCount"] = OLD_MODES[mode].pages ? 0 : 1;
  doc["statusBarBookProgressPercentage"] = OLD_MODES[mode].percent ? 0 : 1;
  doc["statusBarClock"] = S::STATUS_BAR_CLOCK_RIGHT;
  SETTINGS.fromJson(doc.as<JsonVariantConst>());
}

void legacyFilesKeepWhatTheyShowed() {
  for (int mode = 0; mode < S::READER_STATUS_BAR_MODE_COUNT; ++mode) {
    loadLegacy(mode);
    expect(same(seen(), OLD_MODES[mode]), "legacy file shows what its mode showed", mode);
    if (mode == S::READER_STATUS_BAR_OFF) continue;
    // The file written back carries the switches that are in effect, so the web
    // page and the next boot read the same thing the reader draws.
    JsonDocument saved;
    SETTINGS.toJson(saved);
    expect(saved["statusBarItemsMode"].as<int>() == mode, "saved file marks the switches as this mode's", mode);
    expect((saved["statusBarTitle"].as<int>() != S::HIDE_TITLE) == OLD_MODES[mode].title, "saved title switch",
           mode);
    expect((saved["statusBarChapterPageCount"].as<int>() == 1) == OLD_MODES[mode].pages, "saved pages switch", mode);
    expect((saved["statusBarBookProgressPercentage"].as<int>() == 1) == OLD_MODES[mode].percent,
           "saved percent switch", mode);
    SETTINGS.fromJson(saved.as<JsonVariantConst>());
    expect(same(seen(), OLD_MODES[mode]), "reloading the written file changes nothing", mode);
  }
}

void eachSwitchStandsAlone() {
  loadLegacy(S::READER_STATUS_BAR_DEFAULT);
  for (int mask = 0; mask < 8; ++mask) {
    SETTINGS.adoptReaderStatusItems();
    SETTINGS.statusBarTitle = (mask & 1) ? S::CHAPTER_TITLE : S::HIDE_TITLE;
    SETTINGS.statusBarChapterPageCount = (mask & 2) ? 1 : 0;
    SETTINGS.statusBarBookProgressPercentage = (mask & 4) ? 1 : 0;
    const Seen want{(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0, true, true};
    expect(same(seen(), want), "one switch per value, battery and clock untouched", 100 + mask);
  }
}

void switchesStartFromTheChosenMode() {
  // Chapter name & clock, then the percentage switched on: the mode's own
  // corners stay (clock, no battery) and its text becomes the starting point.
  loadLegacy(S::READER_STATUS_BAR_CHAPTER_CLOCK);
  SETTINGS.adoptReaderStatusItems();
  SETTINGS.statusBarBookProgressPercentage = 1;
  expect(same(seen(), {true, false, true, false, true}), "switch on top of a mode keeps its corners",
         S::READER_STATUS_BAR_CHAPTER_CLOCK);
}

void modeChosenElsewhereShowsItsName() {
  // The reader menu and the web page write the mode byte directly. Picking a mode
  // there must show that mode's items, and coming back finds the switches again.
  loadLegacy(S::READER_STATUS_BAR_DEFAULT);
  SETTINGS.adoptReaderStatusItems();
  SETTINGS.statusBarTitle = S::HIDE_TITLE;
  SETTINGS.readerStatusBarMode = S::READER_STATUS_BAR_CHAPTER_BATTERY;
  expect(same(seen(), OLD_MODES[S::READER_STATUS_BAR_CHAPTER_BATTERY]), "mode picked elsewhere shows its name",
         S::READER_STATUS_BAR_CHAPTER_BATTERY);
  SETTINGS.readerStatusBarMode = S::READER_STATUS_BAR_DEFAULT;
  expect(same(seen(), {false, true, true, true, true}), "back on the switched mode, the switches return",
         S::READER_STATUS_BAR_DEFAULT);
}

void currentFilesKeepTheirSwitches() {
  // A file already carrying the marker keeps the user's switches as saved.
  JsonDocument doc;
  doc["readerStatusBarMode"] = S::READER_STATUS_BAR_DEFAULT;
  doc["statusBarItemsMode"] = S::READER_STATUS_BAR_DEFAULT;
  doc["statusBarTitle"] = S::CHAPTER_TITLE;
  doc["statusBarChapterPageCount"] = 0;
  doc["statusBarBookProgressPercentage"] = 1;
  SETTINGS.fromJson(doc.as<JsonVariantConst>());
  expect(same(seen(), {true, false, true, true, true}), "marked file keeps its switches", S::READER_STATUS_BAR_DEFAULT);
}

}  // namespace

int main() {
  // Defaults on a fresh card are the full bar, as before.
  expect(same(seen(), OLD_MODES[S::READER_STATUS_BAR_DEFAULT]), "fresh defaults", S::READER_STATUS_BAR_DEFAULT);
  legacyFilesKeepWhatTheyShowed();
  eachSwitchStandsAlone();
  switchesStartFromTheChosenMode();
  modeChosenElsewhereShowsItsName();
  currentFilesKeepTheirSwitches();
  std::printf("reader_status_items:%s (%d failures)\n", failures ? "RED" : "GREEN", failures);
  return failures ? 1 : 0;
}
