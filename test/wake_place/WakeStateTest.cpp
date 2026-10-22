// state.json carries the place a sleep kept: Connections (kind 5) goes through the production toJson / fromJson.
#include <gtest/gtest.h>

#include <string>

#include "CrossPointState.h"

namespace {
// The one instance (its constructor is private): the place goes to a document, is wiped, and read back.
WakePlace roundTrip(const WakePlace& kept) {
  auto& state = CrossPointState::getInstance();
  state.wake = kept;
  JsonDocument doc;
  state.toJson(doc);
  std::string text;
  serializeJson(doc, text);
  state.wake = {};
  JsonDocument parsed;
  EXPECT_FALSE(deserializeJson(parsed, text));
  EXPECT_TRUE(state.fromJson(parsed.as<JsonVariantConst>()));
  return state.wake;
}
}  // namespace

TEST(WakeState, AConnectionsPlaceSurvivesTheCard) {
  const auto out = roundTrip(WakePlace{WakeKind::Connect, 0, 0, 5, "/books/dune.epub", ""});  // font store, from a book
  EXPECT_EQ(out.kind, WakeKind::Connect);
  EXPECT_EQ(static_cast<int>(out.kind), 5);
  EXPECT_EQ(out.row, 5);
  EXPECT_EQ(out.path, "/books/dune.epub");
}

TEST(WakeState, ANoneKindLeavesNoKeysAndComesBackNone) {
  auto& state = CrossPointState::getInstance();
  state.wake = {};
  JsonDocument doc;
  state.toJson(doc);
  EXPECT_TRUE(doc["wakeKind"].isNull());
  EXPECT_EQ(roundTrip({}).kind, WakeKind::None);
}

TEST(WakeState, TheKindsOfV1072StillRoundTrip) {
  const auto out = roundTrip(WakePlace{WakeKind::SettingsTab, 0, 3, 0, "", "action/5"});
  EXPECT_EQ(out.kind, WakeKind::SettingsTab);
  EXPECT_EQ(out.tab, 3);
  EXPECT_EQ(out.name, "action/5");
}
