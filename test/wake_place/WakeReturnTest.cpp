// The Settings tab and row a silent restart comes back to ride in RTC memory (util/WakeReturn.h): what a clean
// slot returns, a slot of garbage (a cold boot), a key that is too long, and anything that is not a Settings place.
#include <gtest/gtest.h>

#include <cstring>

#include "util/WakeReturn.h"

namespace {
WakePlace settings(const int tab, const std::string& key) {
  return WakePlace{WakeKind::SettingsTab, 0, static_cast<int16_t>(tab), 0, "", key};
}
}  // namespace

TEST(WakeReturn, ASettingsTabAndRowComeBackAsTheyWentIn) {
  wakereturn::Slot slot;
  wakereturn::store(slot, settings(4, "action/12"));
  const auto out = wakereturn::load(slot);
  EXPECT_EQ(out.kind, WakeKind::SettingsTab);
  EXPECT_EQ(out.tab, 4);
  EXPECT_EQ(out.name, "action/12");
}

TEST(WakeReturn, TabZeroIsAPlaceNotAnEmptySlot) {
  wakereturn::Slot slot;
  wakereturn::store(slot, settings(0, ""));
  EXPECT_EQ(wakereturn::load(slot).kind, WakeKind::SettingsTab);
  EXPECT_EQ(wakereturn::load(slot).tab, 0);
}

TEST(WakeReturn, OnlyASettingsPlaceIsKept) {
  wakereturn::Slot slot;
  wakereturn::store(slot, settings(2, "fontSize"));
  wakereturn::store(slot, WakePlace{WakeKind::HomeTab, 0, 1, 0, "", "x"});
  EXPECT_EQ(wakereturn::load(slot).kind, WakeKind::None) << "a Home place overwrites the slot with nothing";
  wakereturn::store(slot, WakePlace{});
  EXPECT_EQ(wakereturn::load(slot).kind, WakeKind::None);
}

TEST(WakeReturn, GarbageFromAColdBootIsNothing) {
  wakereturn::Slot slot;
  std::memset(&slot, 0xA5, sizeof(slot));
  EXPECT_EQ(wakereturn::load(slot).kind, WakeKind::None) << "tab out of range";
  slot.tabPlusOne = 3;  // a plausible tab, but the key was never ended
  const auto out = wakereturn::load(slot);
  EXPECT_EQ(out.kind, WakeKind::SettingsTab);
  EXPECT_LT(out.name.size(), static_cast<size_t>(wakereturn::KEY_MAX)) << "read stops inside the slot";
}

TEST(WakeReturn, ALongKeyIsCutAndStillEnded) {
  wakereturn::Slot slot;
  wakereturn::store(slot, settings(1, std::string(100, 'k')));
  EXPECT_EQ(wakereturn::load(slot).name.size(), static_cast<size_t>(wakereturn::KEY_MAX - 1));
}

TEST(WakeReturn, ATabPastTheLastIsNotStored) {
  wakereturn::Slot slot;
  wakereturn::store(slot, settings(wakeplace::SETTINGS_TABS, "x"));
  EXPECT_EQ(wakereturn::load(slot).kind, WakeKind::None);
}
