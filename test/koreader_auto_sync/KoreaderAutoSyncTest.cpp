#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "KOReaderAutoSyncPolicy.h"
#include "KOReaderSyncDebt.h"
#include "KOReaderWifiHint.h"

using namespace kosyncauto;

namespace {
SyncDebt debt(const std::string& path, const float pct, const uint16_t page = 3) {
  SyncDebt d;
  d.path = path;
  d.spineIndex = 2;
  d.pageNumber = page;
  d.totalPages = 40;
  d.visibleTextOffset = 1234;
  d.hasVisibleTextOffset = true;
  d.percentage = pct;
  d.leftAt = 1791700000;
  return d;
}

CrossPointPosition resolved(const int spine, const int page) {
  CrossPointPosition p{};
  p.spineIndex = spine;
  p.pageNumber = page;
  p.totalPages = 20;
  p.hasResolvedSpineIndex = true;
  p.hasMappedPage = true;
  return p;
}
}  // namespace

// ---- the gate -------------------------------------------------------------------------------------------------

TEST(AutoSyncGate, OffByDefaultMeansNothingRuns) {
  // Every other input is perfect; the switch alone decides.
  EXPECT_EQ(eligibility(false, true, true, 90000, 60000, true), Eligibility::Disabled);
}

TEST(AutoSyncGate, EveryMissingPieceStopsTheRound) {
  EXPECT_EQ(eligibility(true, false, true, 90000, 60000, true), Eligibility::NoAccount);
  EXPECT_EQ(eligibility(true, true, false, 90000, 60000, true), Eligibility::NoNetwork);
  EXPECT_EQ(eligibility(true, true, true, 90000, 60000, false), Eligibility::RadioBusy);
  EXPECT_EQ(eligibility(true, true, true, 90000, 60000, true), Eligibility::Ready);
}

TEST(AutoSyncGate, HeapFloorsAreTheTlsFloors) {
  EXPECT_EQ(eligibility(true, true, true, MIN_FREE_FOR_TLS - 1, 60000, true), Eligibility::LowHeap);
  EXPECT_EQ(eligibility(true, true, true, 90000, MIN_BLOCK_FOR_TLS - 1, true), Eligibility::LowHeap);
  EXPECT_EQ(eligibility(true, true, true, MIN_FREE_FOR_TLS, MIN_BLOCK_FOR_TLS, true), Eligibility::Ready);
  // X3 after a join, measured 14/10/2026: free 44648, largest 30708.
  EXPECT_EQ(eligibility(true, true, true, 44648, 30708, true), Eligibility::Ready);
}

TEST(AutoSyncGate, RoundCeilingStopsNewRequests) {
  EXPECT_TRUE(canStartRequest(0));
  EXPECT_TRUE(canStartRequest(ROUND_BUDGET_MS - REQUEST_MS));
  EXPECT_FALSE(canStartRequest(ROUND_BUDGET_MS - REQUEST_MS + 1));
  EXPECT_FALSE(canStartRequest(ROUND_BUDGET_MS));
  // The measured X3 path (join 7.1 s, GET 2.1 s, PUT 2.0 s) must fit a single pushed book.
  EXPECT_TRUE(canStartRequest(7100));
  EXPECT_TRUE(canStartRequest(7100 + 2100));
  EXPECT_LE(7100u + 2100u + 2000u, ROUND_BUDGET_MS);
}

// ---- push and pull decisions ----------------------------------------------------------------------------------

TEST(AutoSyncPush, OnlyALocalAheadBookOrAMissingServerCopyIsUploaded) {
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::LocalAhead), PushAction::Push);
  EXPECT_EQ(pushAction(Fetch::NotFound, ProgressComparison::Unknown), PushAction::Push);
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::RemoteAhead), PushAction::DropDebt);
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::Synchronized), PushAction::DropDebt);
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::Unknown), PushAction::DropDebt);
  EXPECT_EQ(pushAction(Fetch::Failed, ProgressComparison::LocalAhead), PushAction::KeepDebt);
}

TEST(AutoSyncPull, OnlyAServerPositionAheadMovesTheBook) {
  EXPECT_EQ(pullAction(Fetch::Found, ProgressComparison::RemoteAhead), PullAction::Apply);
  EXPECT_EQ(pullAction(Fetch::Found, ProgressComparison::LocalAhead), PullAction::Keep);
  EXPECT_EQ(pullAction(Fetch::Found, ProgressComparison::Synchronized), PullAction::Keep);
  EXPECT_EQ(pullAction(Fetch::Found, ProgressComparison::Unknown), PullAction::Keep);
  EXPECT_EQ(pullAction(Fetch::NotFound, ProgressComparison::RemoteAhead), PullAction::Keep);
  EXPECT_EQ(pullAction(Fetch::Failed, ProgressComparison::RemoteAhead), PullAction::Keep);
}

TEST(AutoSyncPull, ComparisonFeedsTheDecisionTheWayTheManualSmartSyncDoes) {
  const auto decide = [](const CrossPointPosition& local, const float lp, const CrossPointPosition& remote,
                         const float rp) {
    return pullAction(Fetch::Found, compareProgress(local, lp, remote, rp));
  };
  // The phone read on: the other machine is ahead, the page moves.
  EXPECT_EQ(decide(resolved(2, 3), 0.20f, resolved(2, 9), 0.30f), PullAction::Apply);
  // This machine is ahead, or level: the page stays.
  EXPECT_EQ(decide(resolved(2, 9), 0.30f, resolved(2, 3), 0.20f), PullAction::Keep);
  EXPECT_EQ(decide(resolved(2, 3), 0.20f, resolved(2, 3), 0.20f), PullAction::Keep);
  // Nothing mapped on either side and no usable percentage: unknown, so the page stays.
  CrossPointPosition none{};
  EXPECT_EQ(decide(none, std::numeric_limits<float>::quiet_NaN(), none, 0.3f), PullAction::Keep);
}

TEST(AutoSyncPush, PercentageAloneDecidesWhenNoPositionIsMapped) {
  // The push decides before the book is opened, with percentages only.
  const CrossPointPosition none{};
  EXPECT_EQ(compareProgress(none, 0.40f, none, 0.30f), ProgressComparison::LocalAhead);
  EXPECT_EQ(compareProgress(none, 0.30f, none, 0.40f), ProgressComparison::RemoteAhead);
  EXPECT_EQ(compareProgress(none, 0.3000f, none, 0.3005f), ProgressComparison::Synchronized);
}

// ---- the list of books owed a push ----------------------------------------------------------------------------

TEST(SyncDebtList, AddingTheSameBookReplacesItAndMakesItNewest) {
  SyncDebtList list;
  list.put(debt("/a.epub", 0.1f));
  list.put(debt("/b.epub", 0.2f));
  list.put(debt("/a.epub", 0.5f, 9));
  ASSERT_EQ(list.size(), 2u);
  EXPECT_EQ(list.at(0).path, "/b.epub");
  EXPECT_EQ(list.at(1).path, "/a.epub");
  EXPECT_FLOAT_EQ(list.at(1).percentage, 0.5f);
  EXPECT_EQ(list.at(1).pageNumber, 9);
}

TEST(SyncDebtList, AFifthBookPushesTheOldestOut) {
  SyncDebtList list;
  for (int i = 0; i < 5; ++i) list.put(debt("/" + std::to_string(i) + ".epub", 0.1f * static_cast<float>(i)));
  ASSERT_EQ(list.size(), MAX_DEBTS);
  EXPECT_EQ(list.at(0).path, "/1.epub");
  EXPECT_EQ(list.at(3).path, "/4.epub");
}

TEST(SyncDebtList, RemoveTakesOnlyThatBook) {
  SyncDebtList list;
  list.put(debt("/a.epub", 0.1f));
  list.put(debt("/b.epub", 0.2f));
  EXPECT_TRUE(list.remove("/a.epub"));
  EXPECT_FALSE(list.remove("/a.epub"));
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list.at(0).path, "/b.epub");
}

TEST(SyncDebtList, RoundTripsThroughTheFileFormat) {
  SyncDebtList list;
  SyncDebt d = debt("/Books/Một quyển.epub", 0.4321f, 17);
  d.hasParagraphIndex = true;
  d.paragraphIndex = 12;
  list.put(d);
  list.put(debt("/b.epub", 0.2f));
  std::string text;
  {
    JsonDocument doc;
    list.toJson(doc);
    serializeJson(doc, text);
  }
  JsonDocument back;
  ASSERT_FALSE(deserializeJson(back, text));
  SyncDebtList loaded;
  ASSERT_TRUE(loaded.fromJson(back));
  ASSERT_EQ(loaded.size(), 2u);
  const SyncDebt& r = loaded.at(0);
  EXPECT_EQ(r.path, "/Books/Một quyển.epub");
  EXPECT_EQ(r.spineIndex, 2);
  EXPECT_EQ(r.pageNumber, 17);
  EXPECT_EQ(r.totalPages, 40);
  EXPECT_TRUE(r.hasVisibleTextOffset);
  EXPECT_EQ(r.visibleTextOffset, 1234u);
  EXPECT_TRUE(r.hasParagraphIndex);
  EXPECT_EQ(r.paragraphIndex, 12);
  EXPECT_NEAR(r.percentage, 0.4321f, 1e-6f);
  EXPECT_EQ(r.leftAt, 1791700000u);
  EXPECT_FALSE(loaded.at(1).hasParagraphIndex);
}

TEST(SyncDebtList, ACutOffFileReadsAsNothingOwed) {
  // A file cut mid-write fails to parse; the list stays empty and nothing is pushed.
  const std::string whole =
      R"({"v":1,"debts":[{"path":"/a.epub","spine":2,"page":3,"pages":40,"pct":0.25,"at":1}]})";
  for (const size_t cut : {size_t{0}, size_t{9}, whole.size() / 2, whole.size() - 1}) {
    JsonDocument doc;
    const bool parsed = !deserializeJson(doc, whole.substr(0, cut));
    SyncDebtList list;
    list.put(debt("/stale.epub", 0.1f));
    if (parsed) {
      list.fromJson(doc);
    } else {
      list.fromJson(JsonVariantConst());
    }
    EXPECT_TRUE(list.empty()) << "cut at " << cut;
  }
}

TEST(SyncDebtList, WrongShapeIsRefused) {
  for (const char* text : {"[]", "{}", R"({"debts":3})", R"({"debts":"x"})", "42"}) {
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, text)) << text;
    SyncDebtList list;
    list.put(debt("/stale.epub", 0.1f));
    EXPECT_FALSE(list.fromJson(doc)) << text;
    EXPECT_TRUE(list.empty()) << text;
  }
}

TEST(SyncDebtList, BadEntriesAreSkippedGoodOnesKept) {
  const char* text = R"({"v":1,"debts":[
    {"path":"","spine":1,"page":1,"pages":1,"pct":0.1},
    {"spine":1,"page":1,"pages":1,"pct":0.1},
    {"path":"/range.epub","spine":70000,"page":1,"pages":1,"pct":0.1},
    {"path":"/pct.epub","spine":1,"page":1,"pages":1,"pct":1.5},
    {"path":"/nopct.epub","spine":1,"page":1,"pages":1},
    7,
    {"path":"/ok.epub","spine":4,"page":5,"pages":60,"pct":0.5}]})";
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, text));
  SyncDebtList list;
  ASSERT_TRUE(list.fromJson(doc));
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list.at(0).path, "/ok.epub");
  EXPECT_EQ(list.at(0).spineIndex, 4);
}

TEST(SyncDebtList, AFileWithMoreThanTheCapKeepsTheNewestFour) {
  std::string text = R"({"v":1,"debts":[)";
  for (int i = 0; i < 7; ++i) {
    if (i) text += ",";
    text += R"({"path":"/)" + std::to_string(i) + R"(.epub","spine":1,"page":1,"pages":9,"pct":0.1})";
  }
  text += "]}";
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, text));
  SyncDebtList list;
  ASSERT_TRUE(list.fromJson(doc));
  ASSERT_EQ(list.size(), MAX_DEBTS);
  EXPECT_EQ(list.at(0).path, "/3.epub");
  EXPECT_EQ(list.at(3).path, "/6.epub");
}

// ---- progress.bin, read the way EpubReaderActivity::loadBook reads it ----------------------------------------

TEST(SavedPlace, ReadsEveryFileLengthTheReaderWrites) {
  const uint8_t four[] = {0x05, 0x00, 0x0A, 0x00};
  const auto a = parseProgressFile(four, sizeof(four));
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a->spineIndex, 5);
  EXPECT_EQ(a->pageNumber, 10);
  EXPECT_EQ(a->totalPages, 0);
  EXPECT_FALSE(a->hasVisibleTextOffset);

  const uint8_t six[] = {0x05, 0x01, 0x0A, 0x00, 0x2C, 0x01};
  const auto b = parseProgressFile(six, sizeof(six));
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->spineIndex, 261);
  EXPECT_EQ(b->totalPages, 300);
  EXPECT_FALSE(b->hasVisibleTextOffset);

  const uint8_t ten[] = {0x02, 0x00, 0x03, 0x00, 0x28, 0x00, 0xD2, 0x04, 0x01, 0x00};
  const auto c = parseProgressFile(ten, sizeof(ten));
  ASSERT_TRUE(c.has_value());
  EXPECT_TRUE(c->hasVisibleTextOffset);
  EXPECT_EQ(c->visibleTextOffset, 0x000104D2u);
}

TEST(SavedPlace, OtherLengthsAreNotAProgressFile) {
  const uint8_t bytes[12] = {};
  for (const size_t n : {size_t{0}, size_t{1}, size_t{3}, size_t{5}, size_t{7}, size_t{9}, size_t{11}}) {
    EXPECT_FALSE(parseProgressFile(bytes, n).has_value()) << n;
  }
}

TEST(SavedPlace, TheOldLastPageSentinelMeansPageZero) {
  const uint8_t four[] = {0x01, 0x00, 0xFF, 0xFF};
  EXPECT_EQ(parseProgressFile(four, sizeof(four))->pageNumber, 0);
}

// ---- the 30 minute rule for the wake pull ----------------------------------------------------------------------

TEST(WakePullSleep, ThirtyMinutesOrMoreWithATrustedClockPulls) {
  const uint32_t at = 1791700000;
  EXPECT_TRUE(sleptLongEnough(at, at + PULL_MIN_SLEEP_SECONDS, true));
  EXPECT_TRUE(sleptLongEnough(at, at + 8 * 3600, true));
  EXPECT_TRUE(sleptLongEnough(at, at + 90 * 86400, true));
}

TEST(WakePullSleep, AShorterSleepOpensTheBookAtOnce) {
  const uint32_t at = 1791700000;
  EXPECT_FALSE(sleptLongEnough(at, at, true));
  EXPECT_FALSE(sleptLongEnough(at, at + 5 * 60, true));
  EXPECT_FALSE(sleptLongEnough(at, at + PULL_MIN_SLEEP_SECONDS - 1, true));
}

TEST(WakePullSleep, NoTrustedClockCountsAsAShortSleep) {
  const uint32_t at = 1791700000;
  EXPECT_FALSE(sleptLongEnough(at, at + 3600, false));  // clock not trusted on wake
  EXPECT_FALSE(sleptLongEnough(0, at + 3600, true));    // no stamp: it was not trusted going to sleep
  EXPECT_FALSE(sleptLongEnough(at, at - 10, true));     // the clock went backwards
}

// ---- remembered access point -----------------------------------------------------------------------------------

namespace {
WifiHint goodHint() {
  WifiHint h;
  h.ssid = "nha-tuan";
  h.channel = 6;
  const uint8_t mac[6] = {0x3c, 0x84, 0x6a, 0x11, 0x22, 0x33};
  for (int i = 0; i < 6; ++i) h.bssid[i] = mac[i];
  return h;
}
}  // namespace

TEST(WifiHint, RoundTripsAndIsUsableOnlyForItsOwnNetwork) {
  JsonDocument doc;
  goodHint().toJson(doc);
  std::string text;
  serializeJson(doc, text);
  EXPECT_NE(text.find("3c846a112233"), std::string::npos);
  JsonDocument back;
  ASSERT_FALSE(deserializeJson(back, text));
  WifiHint loaded;
  ASSERT_TRUE(loaded.fromJson(back));
  EXPECT_TRUE(loaded == goodHint());
  EXPECT_TRUE(loaded.usableFor("nha-tuan"));
  EXPECT_FALSE(loaded.usableFor("van-phong"));
}

TEST(WifiHint, MalformedHintsAreRefusedSoTheJoinGoesTheNormalWay) {
  const auto refused = [](const char* json) {
    JsonDocument doc;
    EXPECT_FALSE(deserializeJson(doc, json)) << json;
    WifiHint hint = goodHint();
    EXPECT_FALSE(hint.fromJson(doc)) << json;
    EXPECT_TRUE(hint.ssid.empty()) << json;  // a refused read leaves nothing usable behind
    EXPECT_FALSE(hint.wellFormed()) << json;
  };
  refused(R"({})");
  refused(R"({"ssid":"x","ch":6})");
  refused(R"({"ssid":"x","ch":0,"bssid":"3c846a112233"})");
  refused(R"({"ssid":"x","ch":15,"bssid":"3c846a112233"})");
  refused(R"({"ssid":"x","ch":300,"bssid":"3c846a112233"})");
  refused(R"({"ssid":"x","ch":6,"bssid":"3c846a11223"})");
  refused(R"({"ssid":"x","ch":6,"bssid":"3c846a11223g"})");
  refused(R"({"ssid":"x","ch":6,"bssid":"000000000000"})");
  refused(R"({"ssid":"x","ch":6,"bssid":"ffffffffffff"})");
  refused(R"({"ssid":"x","ch":6,"bssid":"3d846a112233"})");  // multicast bit set
  refused(R"({"ssid":"","ch":6,"bssid":"3c846a112233"})");
  refused(R"({"ssid":3,"ch":6,"bssid":"3c846a112233"})");
}
