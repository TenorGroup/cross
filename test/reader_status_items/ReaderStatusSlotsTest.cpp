#include <cstdio>

#include "SettingsList.h"

using S = CrossPointSettings;
int failures = 0;
void check(bool ok, const char* detail) {
  if (!ok) { ++failures; std::printf("FAIL: %s\n", detail); }
}

int main() {
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
    check(SETTINGS.setReaderStatusItem(row, value), "each slot accepts every value");
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
