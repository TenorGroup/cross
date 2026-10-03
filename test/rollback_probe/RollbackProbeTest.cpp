// The rollback test's crash counter must end every crash run within its cap, from any RTC content,
// so a locked unit can never be left in a crash loop; the verdict reads the newest otadata entry.
#include <gtest/gtest.h>

#include <random>

#include "platform/RollbackProbe.h"

using rollback_probe::CrashCounter;

namespace {
// Boots until one does not crash; a counter that never stops would hang the test, so cap it.
uint32_t crashesUntilNormalBoot(CrashCounter& c) {
  uint32_t crashes = 0;
  while (rollback_probe::takeCrash(c) && crashes < 100) ++crashes;
  return crashes;
}
}  // namespace

TEST(RollbackProbe, ArmedOnceCrashesOneBootThenBootsNormally) {
  CrashCounter c{};
  rollback_probe::arm(c, 1);
  EXPECT_TRUE(rollback_probe::takeCrash(c));
  EXPECT_FALSE(rollback_probe::takeCrash(c));
  EXPECT_FALSE(rollback_probe::takeCrash(c));
}

TEST(RollbackProbe, CrashCountIsClampedToOneToThree) {
  CrashCounter c{};
  rollback_probe::arm(c, 0);
  EXPECT_EQ(crashesUntilNormalBoot(c), 1u);
  rollback_probe::arm(c, 2);
  EXPECT_EQ(crashesUntilNormalBoot(c), 2u);
  rollback_probe::arm(c, 99);
  EXPECT_EQ(crashesUntilNormalBoot(c), rollback_probe::kMaxCrashes);
}

TEST(RollbackProbe, MagicAloneDoesNotArm) {
  CrashCounter c{rollback_probe::kMagic, 1, 0};
  EXPECT_FALSE(rollback_probe::takeCrash(c));
  EXPECT_EQ(c.magic, 0u);
}

TEST(RollbackProbe, AnyPowerUpContentEndsWithinTheCap) {
  std::mt19937 rng(20261004);
  for (int i = 0; i < 200000; ++i) {
    CrashCounter c{rng(), rng() % 8, rng()};
    if (i % 4 == 0) c.magic = rollback_probe::kMagic;  // garbage that matches the magic
    ASSERT_LE(crashesUntilNormalBoot(c), rollback_probe::kMaxCrashes);
    ASSERT_FALSE(rollback_probe::takeCrash(c));
  }
}

TEST(RollbackProbe, NewestEntryIgnoresBlankAndBadCrcButNotAborted) {
  using E = rollback_probe::OtaEntry;
  const E blank[2] = {{UINT32_MAX, 0, true}, {UINT32_MAX, 0, true}};
  EXPECT_EQ(rollback_probe::newestEntry(blank), -1);
  const E badCrc[2] = {{3, 2, true}, {5, 0, false}};
  EXPECT_EQ(rollback_probe::newestEntry(badCrc), 0);
  const E aborted[2] = {{3, 2, true}, {5, 4, true}};
  EXPECT_EQ(rollback_probe::newestEntry(aborted), 1);
}

TEST(RollbackProbe, VerdictNamesTheBootloaderBehaviour) {
  EXPECT_STREQ(rollback_probe::verdict(4), "rolled_back");
  EXPECT_STREQ(rollback_probe::verdict(0), "none");
  EXPECT_STREQ(rollback_probe::verdict(1), "on_trial");
  EXPECT_STREQ(rollback_probe::verdict(2), "valid");
}
