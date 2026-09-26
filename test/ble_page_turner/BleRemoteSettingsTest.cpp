// Per-remote button tables through the real settings.json code (CrossPointSettings::
// toJson / fromJson, compiled as is): four remotes round-trip intact, garbage slots
// are dropped, a table past 8 slots is cut, a fifth remote is dropped, and an empty
// table keeps its address.

#include <gtest/gtest.h>

#include <cstring>

#include "CrossPointSettings.h"

namespace {

constexpr uint32_t kNextChapterTap = blebinding::makeBinding(0x030102, false, blebinding::Action::NextChapter);
constexpr uint32_t kPrevChapterHold = blebinding::makeBinding(0x030008, true, blebinding::Action::PrevChapter);

TEST(BleRemoteSettingsTest, FourRemotesRoundTripThroughTheSettingsFile) {
  CrossPointSettings& settings = SETTINGS;
  settings.bleRemoteCount = 0;
  const char* addrs[4] = {"11:22:33:44:55:01", "11:22:33:44:55:02", "11:22:33:44:55:03", "11:22:33:44:55:04"};
  for (int i = 0; i < 4; ++i) {
    blebinding::RemoteTable* t = blebinding::editableTable(settings.bleRemotes, settings.bleRemoteCount, addrs[i], "");
    ASSERT_NE(t, nullptr);
    if (i < 3) ASSERT_TRUE(blebinding::learn(*t, blebinding::Action::NextChapter, 0x030102 + i, false));
    if (i < 2) ASSERT_TRUE(blebinding::learn(*t, blebinding::Action::PrevChapter, 0x030008, true));
  }
  EXPECT_EQ(blebinding::editableTable(settings.bleRemotes, settings.bleRemoteCount, "11:22:33:44:55:05", ""), nullptr)
      << "a fifth remote has no slot";

  JsonDocument saved;
  settings.toJson(saved);
  settings.bleRemoteCount = 0;
  ASSERT_TRUE(settings.fromJson(saved.as<JsonVariantConst>()));

  ASSERT_EQ(settings.bleRemoteCount, 4);
  for (int i = 0; i < 4; ++i) EXPECT_STREQ(settings.bleRemotes[i].addr, addrs[i]);
  EXPECT_EQ(settings.bleRemotes[0].count, 2);
  EXPECT_EQ(settings.bleRemotes[0].bindings[0], kNextChapterTap);
  EXPECT_EQ(settings.bleRemotes[0].bindings[1], kPrevChapterHold);
  EXPECT_EQ(settings.bleRemotes[2].count, 1);
  EXPECT_EQ(settings.bleRemotes[3].count, 0) << "a table cleared by hand keeps its address";
}

TEST(BleRemoteSettingsTest, GarbageSlotsAreDroppedAndOversizedTablesCut) {
  CrossPointSettings& settings = SETTINGS;
  JsonDocument doc;
  JsonArray remotes = doc["bleRemotes"].to<JsonArray>();
  JsonObject a = remotes.add<JsonObject>();
  a["addr"] = "AA:BB:CC:DD:EE:01";
  JsonArray binds = a["binds"].to<JsonArray>();
  binds.add(kNextChapterTap);
  binds.add(0x70030102u);  // action out of range
  binds.add(0x18030102u);  // unknown bit
  binds.add("text");       // not a number
  binds.add(blebinding::makeBinding(0x030100, false, blebinding::Action::NextPage));  // zero value
  for (uint32_t i = 1; i <= 12; ++i) binds.add(blebinding::makeBinding(0x020000 + i, false, blebinding::Action::NextPage));
  JsonObject noAddr = remotes.add<JsonObject>();
  noAddr["binds"].to<JsonArray>().add(kNextChapterTap);
  for (int i = 0; i < 5; ++i) {
    JsonObject r = remotes.add<JsonObject>();
    r["addr"] = "AA:BB:CC:DD:EE:02";
    r["binds"].to<JsonArray>();
  }

  ASSERT_TRUE(settings.fromJson(doc.as<JsonVariantConst>()));
  ASSERT_EQ(settings.bleRemoteCount, blebinding::kMaxRemotes);
  EXPECT_STREQ(settings.bleRemotes[0].addr, "AA:BB:CC:DD:EE:01");
  ASSERT_EQ(settings.bleRemotes[0].count, blebinding::kMaxBindings);
  EXPECT_EQ(settings.bleRemotes[0].bindings[0], kNextChapterTap);
  for (uint8_t i = 0; i < settings.bleRemotes[0].count; ++i) {
    EXPECT_TRUE(blebinding::valid(settings.bleRemotes[0].bindings[i])) << i;
  }
  EXPECT_STREQ(settings.bleRemotes[1].addr, "AA:BB:CC:DD:EE:02");

  // A file from before this version has no key: no table, today's path.
  JsonDocument old;
  old["blePageTurnerEnabled"] = 1;
  ASSERT_TRUE(settings.fromJson(old.as<JsonVariantConst>()));
  EXPECT_EQ(settings.bleRemoteCount, 0);
  JsonDocument resaved;
  settings.toJson(resaved);
  EXPECT_TRUE(resaved["bleRemotes"].isNull()) << "no table, no key";
}

// The reader menu and save quotation actions go into the same 4-bit action field: a
// file written with them reads back with them, and a file from before them reads as it
// always did.
TEST(BleRemoteSettingsTest, ReaderShortcutActionsRoundTripAndOldSlotsStay) {
  CrossPointSettings& settings = SETTINGS;
  const uint32_t menuTap = blebinding::makeBinding(0x030001, false, blebinding::Action::ReaderMenu);
  const uint32_t quoteHold = blebinding::makeBinding(0x030008, true, blebinding::Action::SaveQuote);
  JsonDocument doc;
  JsonObject r = doc["bleRemotes"].to<JsonArray>().add<JsonObject>();
  r["addr"] = "AA:BB:CC:DD:EE:07";
  JsonArray binds = r["binds"].to<JsonArray>();
  binds.add(kNextChapterTap);  // a slot written before the two actions existed
  binds.add(menuTap);
  binds.add(quoteHold);
  ASSERT_TRUE(settings.fromJson(doc.as<JsonVariantConst>()));
  ASSERT_EQ(settings.bleRemoteCount, 1);
  ASSERT_EQ(settings.bleRemotes[0].count, 3);
  EXPECT_EQ(settings.bleRemotes[0].bindings[0], kNextChapterTap);
  EXPECT_EQ(blebinding::lookup(settings.bleRemotes[0], 0x030001, false), blebinding::Action::ReaderMenu);
  EXPECT_EQ(blebinding::lookup(settings.bleRemotes[0], 0x030008, true), blebinding::Action::SaveQuote);

  JsonDocument resaved;
  settings.toJson(resaved);
  settings.bleRemoteCount = 0;
  ASSERT_TRUE(settings.fromJson(resaved.as<JsonVariantConst>()));
  ASSERT_EQ(settings.bleRemoteCount, 1);
  ASSERT_EQ(settings.bleRemotes[0].count, 3);
  EXPECT_EQ(settings.bleRemotes[0].bindings[1], menuTap);
  EXPECT_EQ(settings.bleRemotes[0].bindings[2], quoteHold);
  EXPECT_FALSE(blebinding::valid(blebinding::makeBinding(0x030001, false, static_cast<blebinding::Action>(7))))
      << "an action past the list still reads as garbage";
}

}  // namespace
