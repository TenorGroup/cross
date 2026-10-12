#include <cstdio>

#include "SettingsList.h"

using S = CrossPointSettings;
int failures = 0;
void check(bool ok, const char* detail) {
  if (!ok) { ++failures; std::printf("FAIL: %s\n", detail); }
}

int main() {
  JsonDocument full;
  full["readerStatusBarMode"] = S::READER_STATUS_BAR_DEFAULT;
  full["statusBarItemsMode"] = S::READER_STATUS_BAR_DEFAULT;
  full["statusBarTitle"] = S::CHAPTER_TITLE;
  full["statusBarChapterPageCount"] = 1;
  full["statusBarBookProgressPercentage"] = 1;
  SETTINGS.fromJson(full.as<JsonVariantConst>());
  SETTINGS.setReaderStatusItem(0, S::CHAPTER_TITLE);
  auto edited = SETTINGS.statusBarSpec();
  check(edited.showsTitle() && edited.showChapterPageCount && edited.showBookProgressPercent &&
        edited.showBattery && edited.showsClock(), "editing the full bar retains all five facts");
  JsonDocument savedFull;
  SETTINGS.toJson(savedFull);
  SETTINGS.fromJson(savedFull.as<JsonVariantConst>());
  check(SETTINGS.statusBarSpec().showBookProgressPercent, "full bar percentage survives saving and loading");
  for (const int version : {-1, 1}) for (const int left : {S::STATUS_SLOT_CLOCK, S::STATUS_SLOT_BOOK_PERCENT}) {
    JsonDocument old;
    old["readerStatusBarMode"] = S::READER_STATUS_BAR_DEFAULT;
    old["statusBarItemsMode"] = S::READER_STATUS_BAR_DEFAULT;
    old["readerStatusSlotsEnabled"] = 1;
    old["readerStatusTop"] = S::CHAPTER_TITLE;
    old["readerStatusLeft"] = left;
    old["readerStatusCenter"] = S::STATUS_SLOT_CHAPTER_PAGES;
    old["readerStatusRight"] = S::STATUS_SLOT_BATTERY;
    if (version >= 0) old["readerStatusVersion"] = version;
    SETTINGS.fromJson(old.as<JsonVariantConst>());
    const bool moves = version < 1 && left != S::STATUS_SLOT_BOOK_PERCENT;
    check(SETTINGS.readerStatusCenter == (moves ? S::STATUS_SLOT_CHAPTER_AND_BOOK_PROGRESS : S::STATUS_SLOT_CHAPTER_PAGES),
          "an old centre of chapter pages gains the book percent once, unless a corner already shows it");
    JsonDocument saved;
    SETTINGS.toJson(saved);
    check((saved["readerStatusVersion"] | 0) == 1, "the status bar version is saved");
    SETTINGS.fromJson(saved.as<JsonVariantConst>());
    check(SETTINGS.readerStatusCenter == (moves ? S::STATUS_SLOT_CHAPTER_AND_BOOK_PROGRESS : S::STATUS_SLOT_CHAPTER_PAGES),
          "a saved choice is not converted again");
  }
  JsonDocument chosen;
  chosen["readerStatusBarMode"] = S::READER_STATUS_BAR_DEFAULT;
  chosen["statusBarItemsMode"] = S::READER_STATUS_BAR_DEFAULT;
  chosen["readerStatusSlotsEnabled"] = 1;
  chosen["readerStatusCenter"] = S::STATUS_SLOT_CHAPTER_PAGES;
  chosen["readerStatusVersion"] = 1;
  SETTINGS.fromJson(chosen.as<JsonVariantConst>());
  check(SETTINGS.readerStatusCenter == S::STATUS_SLOT_CHAPTER_PAGES, "chapter pages chosen after the change stay");
  for (const int clockSide : {S::STATUS_BAR_CLOCK_RIGHT, S::STATUS_BAR_CLOCK_LEFT}) {
    JsonDocument corners;
    corners["readerStatusBarMode"] = S::READER_STATUS_BAR_DEFAULT;
    corners["statusBarItemsMode"] = S::READER_STATUS_BAR_DEFAULT;
    corners["statusBarTitle"] = S::CHAPTER_TITLE;
    corners["statusBarChapterPageCount"] = 1;
    corners["statusBarBookProgressPercentage"] = 1;
    SETTINGS.fromJson(corners.as<JsonVariantConst>());
    SETTINGS.statusBarClock = clockSide;
    SETTINGS.setReaderStatusItem(0, S::CHAPTER_TITLE);
    const bool left = clockSide == S::STATUS_BAR_CLOCK_LEFT;
    const std::array<uint8_t, 3> expected{left ? S::STATUS_SLOT_CLOCK : S::STATUS_SLOT_BATTERY,
                                          S::STATUS_SLOT_CHAPTER_AND_BOOK_PROGRESS,
                                          left ? S::STATUS_SLOT_BATTERY : S::STATUS_SLOT_CLOCK};
    check(SETTINGS.statusBarSpec().slots == expected, "editing the default bar keeps its clock and battery corners");
  }
  check(S::STATUS_SLOT_COUNT == 9 && S::STATUS_SLOT_CHAPTER_AND_BOOK_PROGRESS == 8, "combined progress is appended at slot value 8");
  check(S::STATUS_SLOT_NONE == 0 && S::STATUS_SLOT_CLOCK == 1 && S::STATUS_SLOT_BATTERY == 2 &&
        S::STATUS_SLOT_CHAPTER_PAGES == 3 && S::STATUS_SLOT_BOOK_PERCENT == 4 &&
        S::STATUS_SLOT_CHAPTER_ETA == 5 && S::STATUS_SLOT_BOOK_ETA == 6, "saved slot values retain their IDs");
  check(S::READER_STATUS_BAR_MODE_COUNT == 8, "old mode values plus two appended presets");
  JsonDocument old;
  old["readerStatusBarMode"] = 2;
  old["statusBarItemsMode"] = 2;
  old["statusBarTitle"] = S::CHAPTER_TITLE;
  SETTINGS.fromJson(old.as<JsonVariantConst>());
  check(!SETTINGS.statusBarSpec().slotsEnabled, "old file retains old bar");
  SETTINGS.setReaderStatusItem(1, S::STATUS_SLOT_BOOK_PERCENT);
  check(SETTINGS.statusBarSpec().topTitleMode == S::CHAPTER_TITLE, "editing a legacy bar retains its title");
  SETTINGS.readerStatusBarMode = S::READER_STATUS_BAR_CLOCK_CHAPTER_PROGRESS;
  auto spec = SETTINGS.statusBarSpec();
  check(spec.slotsEnabled && spec.topTitleMode == S::HIDE_TITLE && spec.slots[0] == S::STATUS_SLOT_CLOCK &&
        spec.slots[1] == S::STATUS_SLOT_CHAPTER_PAGES && spec.slots[2] == S::STATUS_SLOT_NONE,
        "clock and chapter pages preset");
  SETTINGS.readerStatusBarMode = S::READER_STATUS_BAR_BOOK_DETAILS;
  spec = SETTINGS.statusBarSpec();
  check(spec.topTitleMode == S::BOOK_TITLE && spec.slots[2] == S::STATUS_SLOT_BOOK_ETA, "book details preset");
  for (int row = 1; row <= 3; ++row) for (int value = 0; value < S::STATUS_SLOT_COUNT; ++value) {
    const auto before = SETTINGS.statusBarSpec();
    if (value >= S::STATUS_SLOT_CHAPTER_NAME && row != 2) {
      check(!SETTINGS.setReaderStatusItem(row, value), "wide values are rejected outside the center");
      check(SETTINGS.statusBarSpec().slots == before.slots, "rejected choice leaves slots unchanged");
      continue;
    }
    check(SETTINGS.setReaderStatusItem(row, value), "each slot accepts its supported values");
    spec = SETTINGS.statusBarSpec();
    check(spec.slots[row - 1] == value, "changed slot resolves");
    for (int other = 0; other < 3; ++other)
      if (other != row - 1) check(spec.slots[other] == before.slots[other], "other slots retain their values");
    JsonDocument saved;
    SETTINGS.toJson(saved);
    SETTINGS.fromJson(saved.as<JsonVariantConst>());
    check(SETTINGS.statusBarSpec().slots == spec.slots, "slots survive reboot");
  }
  for (int title = 0; title < S::STATUS_BAR_TITLE_COUNT; ++title) {
    check(SETTINGS.setReaderStatusItem(0, title), "top accepts book, chapter, hide");
    JsonDocument saved; SETTINGS.toJson(saved); SETTINGS.fromJson(saved.as<JsonVariantConst>());
    check(SETTINGS.statusBarSpec().topTitleMode == title, "top survives reboot");
  }
  check(!SETTINGS.setReaderStatusItem(0, 255) && !SETTINGS.setReaderStatusItem(3, 255) &&
        !SETTINGS.setReaderStatusItem(4, 1), "bad row/value rejected");
  for (int row = 1; row <= 3; ++row) SETTINGS.setReaderStatusItem(row, S::STATUS_SLOT_NONE);
  SETTINGS.setReaderStatusItem(0, S::HIDE_TITLE);
  check(SETTINGS.setReaderStatusItem(2, 7), "center accepts chapter name");
  check(SETTINGS.statusBarSpec().slots[1] == 7 && SETTINGS.statusBarSpec().textLaneVisible(true),
        "chapter name keeps the bottom text lane visible with top hidden");
  JsonDocument chapterSaved;
  SETTINGS.toJson(chapterSaved);
  SETTINGS.fromJson(chapterSaved.as<JsonVariantConst>());
  check(SETTINGS.readerStatusItem(2) == 7, "chapter name survives reboot");
  chapterSaved["readerStatusLeft"] = 7;
  chapterSaved["readerStatusRight"] = 7;
  SETTINGS.fromJson(chapterSaved.as<JsonVariantConst>());
  check(SETTINGS.readerStatusItem(1) == 0 && SETTINGS.readerStatusItem(3) == 0,
        "invalid side chapter-name values are hidden on load");
  SETTINGS.setReaderStatusItem(2, S::STATUS_SLOT_NONE);
  spec = SETTINGS.statusBarSpec();
  check(!spec.textLaneVisible(true) && !spec.showsTitle(), "all slots and top may be off");
  JsonDocument corrupt;
  SETTINGS.toJson(corrupt);
  corrupt["readerStatusTop"] = -1;
  corrupt["readerStatusLeft"] = 255;
  corrupt["readerStatusCenter"] = -3;
  corrupt["readerStatusRight"] = 1000;
  SETTINGS.fromJson(corrupt.as<JsonVariantConst>());
  spec = SETTINGS.statusBarSpec();
  check(spec.topTitleMode == S::HIDE_TITLE && spec.slots[0] == S::STATUS_SLOT_NONE &&
        spec.slots[1] == S::STATUS_SLOT_NONE && spec.slots[2] == S::STATUS_SLOT_NONE, "corrupt fields hidden");
  SETTINGS.readerStatusBarMode = S::READER_STATUS_BAR_CHAPTER_CLOCK;
  check(!SETTINGS.statusBarSpec().slotsEnabled, "picking old preset uses its old composition");
  SETTINGS.readerStatusBarMode = S::READER_STATUS_BAR_BOOK_DETAILS;
  check(SETTINGS.statusBarSpec().slotsEnabled, "custom slots return on the same marked preset");
  SETTINGS.readerStatusBarMode = S::READER_STATUS_BAR_OFF;
  check(!SETTINGS.statusBarSpec().textLaneVisible(true), "off hides both bars");
  SETTINGS.setReaderStatusItem(1, S::STATUS_SLOT_BATTERY);
  check(SETTINGS.statusBarSpec().slotsEnabled && SETTINGS.statusBarSpec().slots[0] == S::STATUS_SLOT_BATTERY,
        "editing an off bar makes the chosen value visible");
  std::printf("reader_status_slots:%s (%d failures)\n", failures ? "RED" : "GREEN", failures);
  return failures ? 1 : 0;
}
