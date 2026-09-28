// Host test for battshown::next() - see BattShown.h for the rules and why they exist.
#include "BattShown.h"

#include <gtest/gtest.h>

namespace {

constexpr uint32_t MINUTE_MS = 60000;

TEST(BattShown, FirstSampleShowsRawWhileDischarging) {
  battshown::State s;
  EXPECT_EQ(battshown::next(s, 26, false, 0), 26);
}

TEST(BattShown, FirstSampleShowsRawWhileCharging) {
  battshown::State s;
  EXPECT_EQ(battshown::next(s, 7, true, 0), 7);
}

TEST(BattShown, DischargingNeverRisesWhenRawTicksUp) {
  battshown::State s;
  battshown::next(s, 40, false, 0);
  // The gauge briefly reports a higher SOC than what's shown (an EDV/OCV
  // correction upward); the displayed value must hold, not rise.
  EXPECT_EQ(battshown::next(s, 45, false, 1000), 40);
  EXPECT_EQ(battshown::next(s, 41, false, 2000), 40);
}

TEST(BattShown, DischargingDropsAtMostOnePointPerMinute) {
  battshown::State s;
  battshown::next(s, 50, false, 0);
  // raw drops far below shown at t=0, but shown may only step down once the
  // 60s cooldown has elapsed, one point per call.
  EXPECT_EQ(battshown::next(s, 30, false, 100), 50) << "too soon to step";
  EXPECT_EQ(battshown::next(s, 30, false, MINUTE_MS - 1), 50) << "still too soon";
  EXPECT_EQ(battshown::next(s, 30, false, MINUTE_MS), 49) << "one minute elapsed: one point";
  EXPECT_EQ(battshown::next(s, 30, false, MINUTE_MS + 500), 49) << "cooldown restarted";
  EXPECT_EQ(battshown::next(s, 30, false, 2 * MINUTE_MS), 48);
}

TEST(BattShown, DischargingEmptyBypassesTheRateLimit) {
  battshown::State s;
  battshown::next(s, 26, false, 0);
  // raw goes straight to empty seconds later: no 60s-per-point wait allowed,
  // the user must be warned before the device dies.
  EXPECT_EQ(battshown::next(s, 0, false, 5000), 0);
}

TEST(BattShown, DischargingEmptyStaysAtRawEvenAsRawRises) {
  battshown::State s;
  battshown::next(s, 26, false, 0);
  battshown::next(s, 0, false, 5000);
  // raw ticks up slightly while still discharging (e.g. 0 -> 1): shown must
  // never rise, so it stays put rather than following raw back up.
  EXPECT_EQ(battshown::next(s, 1, false, 6000), 0);
}

TEST(BattShown, ChargingRisesImmediatelyRegardlessOfElapsedTime) {
  battshown::State s;
  battshown::next(s, 7, true, 0);
  EXPECT_EQ(battshown::next(s, 55, true, 1000), 55) << "no rate limit while charging";
  EXPECT_EQ(battshown::next(s, 72, true, 2000), 72);
}

TEST(BattShown, ChargingNeverDrops) {
  battshown::State s;
  battshown::next(s, 60, true, 0);
  // A charging raw sample that reads lower than what's shown (a transient
  // dip) must not pull the displayed value down.
  EXPECT_EQ(battshown::next(s, 55, true, 1000), 60);
}

TEST(BattShown, SwitchingFromChargingToDischargingKeepsShownAsTheStart) {
  battshown::State s;
  battshown::next(s, 60, true, 0);
  // Unplugged; raw is already below the shown value. The very next sample
  // starts a fresh 60s cooldown rather than stepping down at once.
  EXPECT_EQ(battshown::next(s, 58, false, 1000), 60);
  EXPECT_EQ(battshown::next(s, 58, false, MINUTE_MS), 59);
}

TEST(BattShown, SwitchingFromDischargingToChargingKeepsShownAsTheStart) {
  battshown::State s;
  battshown::next(s, 40, false, 0);
  EXPECT_EQ(battshown::next(s, 42, true, 1000), 42) << "charging rises immediately from 40";
}

TEST(BattShown, WakeFromDeepSleepStartsFreshAtRaw) {
  // A fresh State models a reboot/wake (the object is a plain global, so
  // nothing about the pre-sleep value survives deep sleep): the first call
  // after wake must equal raw immediately, not resume any prior smoothing.
  battshown::State afterWake;
  EXPECT_EQ(battshown::next(afterWake, 12, false, 999999), 12);
}

// The user's own two reported sequences.

TEST(BattShown, UserSequenceDischarge26To0) {
  battshown::State s;
  uint32_t t = 0;
  EXPECT_EQ(battshown::next(s, 26, false, t), 26);
  t += 30000;  // half a minute later, still discharging
  EXPECT_EQ(battshown::next(s, 25, false, t), 26) << "cooldown not elapsed yet";
  t += 45000;  // past one minute total since the last step
  EXPECT_EQ(battshown::next(s, 25, false, t), 25) << "one point after the cooldown";
  t += 10000;  // gauge's EDV0 correction: straight to empty
  EXPECT_EQ(battshown::next(s, 0, false, t), 0) << "empty must warn immediately, not ease down";
}

TEST(BattShown, UserSequenceCharge7Then55Then72) {
  battshown::State s;
  uint32_t t = 0;
  EXPECT_EQ(battshown::next(s, 7, true, t), 7) << "first sample after plugging in";
  t += 60000;  // "1 minute" per the report
  EXPECT_EQ(battshown::next(s, 55, true, t), 55) << "charging shows the jump immediately";
  t += 120000;  // "2 minutes later"
  EXPECT_EQ(battshown::next(s, 72, true, t), 72);
}

}  // namespace
