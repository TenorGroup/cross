#include <gtest/gtest.h>

#include "PanelChip.h"
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

// The X3 probe's three VER bytes ride along for the Settings row that names the panel chip: a
// wake shows the bytes the probing boot read, without probing again.
TEST(PanelMemo, WakeKeepsTheVerBytesOfTheProbe) {
  const uint8_t ver[3] = {0x00, 0x00, 0x02};
  const auto memo = panelmemo::make(UC8279, 0, ver);
  uint8_t back[3] = {0xAA, 0xAA, 0xAA};
  ASSERT_TRUE(panelmemo::readVer(memo, true, back));
  EXPECT_EQ(back[0], 0x00);
  EXPECT_EQ(back[1], 0x00);
  EXPECT_EQ(back[2], 0x02);
  EXPECT_FALSE(panelmemo::readVer(memo, false, back));
  // A probe that read no VER leaves none; the controller still comes back.
  const auto without = panelmemo::make(UC8279, 0);
  EXPECT_FALSE(panelmemo::readVer(without, true, back));
  uint8_t controller = 0, variant = 0;
  EXPECT_TRUE(panelmemo::read(without, true, controller, variant));
}

TEST(PanelMemo, VerOnlyCountsNextToItsOwnController) {
  const uint8_t ver[3] = {0x12, 0x34, 0x56};
  uint8_t back[3] = {};
  auto memo = panelmemo::make(UC8279, 0, ver);
  memo.ver ^= 1u << 4;
  EXPECT_FALSE(panelmemo::readVer(memo, true, back));
  memo = panelmemo::make(UC8279, 0, ver);
  memo.verCheck ^= 1u;
  EXPECT_FALSE(panelmemo::readVer(memo, true, back));
  // VER bytes copied next to another controller word, or next to a broken one, are not VER.
  const auto other = panelmemo::make(UC8253, 0, ver);
  memo = panelmemo::make(UC8279, 0, ver);
  memo.ver = other.ver;
  memo.verCheck = other.verCheck;
  EXPECT_FALSE(panelmemo::readVer(memo, true, back));
  memo = panelmemo::make(UC8279, 0, ver);
  memo.check ^= 1u;
  EXPECT_FALSE(panelmemo::readVer(memo, true, back));
  for (const panelmemo::Memo garbage : {panelmemo::Memo{0, 0, 0, 0}, panelmemo::Memo{~0u, ~0u, ~0u, ~0u}}) {
    EXPECT_FALSE(panelmemo::readVer(garbage, true, back));
  }
}

TEST(PanelChip, RowTextNamesControllerVerAndOemRecord) {
  const uint8_t ver[3] = {0x00, 0x0A, 0xFF};
  EXPECT_EQ(panelchip::text("UC8279", ver, 2), "UC8279, VER 00 0A FF, NVS 2");
  EXPECT_EQ(panelchip::text("UC8253", nullptr, 3), "UC8253, NVS 3");
  EXPECT_EQ(panelchip::text("UC8279", ver, -1), "UC8279, VER 00 0A FF");
  EXPECT_EQ(panelchip::text("SSD1677", nullptr, -1), "SSD1677");
  EXPECT_EQ(panelchip::text("UC8179", nullptr, 255), "UC8179, NVS 255");
}
