#include <gtest/gtest.h>

#include "PanelMemo.h"

namespace {
constexpr uint8_t UC8279 = 6;
constexpr uint8_t UC8253 = 2;
}  // namespace

TEST(PanelMemo, WakeFromDeepSleepReusesTheProbedController) {
  const auto memo = panelmemo::make(UC8279, 0x66);
  uint8_t controller = 0, variant = 0;
  ASSERT_TRUE(panelmemo::read(memo, true, controller, variant));
  EXPECT_EQ(controller, UC8279);
  EXPECT_EQ(variant, 0x66);
}

TEST(PanelMemo, EveryOtherResetProbesAgain) {
  const auto memo = panelmemo::make(UC8279, 0);
  uint8_t controller = UC8253, variant = 0;
  EXPECT_FALSE(panelmemo::read(memo, false, controller, variant));
  EXPECT_EQ(controller, UC8253);
}

TEST(PanelMemo, WhatPowerLossLeavesInRtcMemoryIsNoMemo) {
  uint8_t controller = UC8253, variant = 0;
  for (const panelmemo::Memo garbage : {panelmemo::Memo{0, 0}, panelmemo::Memo{0xFFFFFFFFu, 0xFFFFFFFFu},
                                        panelmemo::Memo{0xDEADBEEFu, 0x12345678u}}) {
    EXPECT_FALSE(panelmemo::read(garbage, true, controller, variant));
  }
  // One flipped bit in either word, or a word without the tag, is not a memo either.
  auto memo = panelmemo::make(UC8279, 0);
  memo.word ^= 1u << 9;
  EXPECT_FALSE(panelmemo::read(memo, true, controller, variant));
  memo = panelmemo::make(UC8279, 0);
  memo.check ^= 1u;
  EXPECT_FALSE(panelmemo::read(memo, true, controller, variant));
  const uint32_t untagged = 0x00000600u;
  EXPECT_FALSE(panelmemo::read(panelmemo::Memo{untagged, ~untagged}, true, controller, variant));
  EXPECT_EQ(controller, UC8253);
}

TEST(PanelMemo, EachControllerRoundTrips) {
  for (uint8_t value = 0; value <= 8; ++value) {
    uint8_t controller = 0xFF, variant = 0xFF;
    ASSERT_TRUE(panelmemo::read(panelmemo::make(value, value), true, controller, variant));
    EXPECT_EQ(controller, value);
    EXPECT_EQ(variant, value);
  }
}
