#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "KOReaderAutoSyncPolicy.h"
#include "KOReaderSyncDebt.h"
#include "KOReaderSyncMark.h"
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

TEST(AutoSyncGate, ARequestIsNeverGivenLessThanTheTimeItMeasuredAt) {
  // The mapping before the upload can use up the round: the PUT still gets REQUEST_MS (measured 2.0 s), not 1 s.
  EXPECT_EQ(requestTimeoutMs(0), REQUEST_TIMEOUT_MAX_MS);
  EXPECT_EQ(requestTimeoutMs(9000), REQUEST_TIMEOUT_MAX_MS);
  EXPECT_EQ(requestTimeoutMs(10000), 5000u);
  EXPECT_EQ(requestTimeoutMs(ROUND_BUDGET_MS - REQUEST_MS), REQUEST_MS);
  EXPECT_EQ(requestTimeoutMs(14000), REQUEST_MS);
  EXPECT_EQ(requestTimeoutMs(ROUND_BUDGET_MS), REQUEST_MS);
  EXPECT_EQ(requestTimeoutMs(ROUND_BUDGET_MS + 4000), REQUEST_MS);  // past the round: still the floor
  EXPECT_GE(requestTimeoutMs(14000), 2000u);                        // longer than the 2.0 s PUT
}

// ---- push and pull decisions ----------------------------------------------------------------------------------

TEST(AutoSyncPush, OnlyALocalAheadBookOrAMissingServerCopyIsUploaded) {
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::LocalAhead, false, false), PushAction::Push);
  EXPECT_EQ(pushAction(Fetch::NotFound, ProgressComparison::Unknown, false, false), PushAction::Push);
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::RemoteAhead, false, false), PushAction::DropDebt);
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::Synchronized, false, false), PushAction::DropDebt);
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::Unknown, false, false), PushAction::DropDebt);
  EXPECT_EQ(pushAction(Fetch::Failed, ProgressComparison::LocalAhead, false, false), PushAction::KeepDebt);
}

TEST(AutoSyncPull, OnlyAServerPositionAheadMovesTheBook) {
  EXPECT_EQ(pullAction(Fetch::Found, ProgressComparison::RemoteAhead, false), PullAction::Apply);
  EXPECT_EQ(pullAction(Fetch::Found, ProgressComparison::LocalAhead, false), PullAction::Keep);
  EXPECT_EQ(pullAction(Fetch::Found, ProgressComparison::Synchronized, false), PullAction::Keep);
  EXPECT_EQ(pullAction(Fetch::Found, ProgressComparison::Unknown, false), PullAction::Keep);
  EXPECT_EQ(pullAction(Fetch::NotFound, ProgressComparison::RemoteAhead, false), PullAction::Keep);
  EXPECT_EQ(pullAction(Fetch::Failed, ProgressComparison::RemoteAhead, false), PullAction::Keep);
}

TEST(AutoSyncPull, ComparisonFeedsTheDecisionTheWayTheManualSmartSyncDoes) {
  const auto decide = [](const CrossPointPosition& local, const float lp, const CrossPointPosition& remote,
                         const float rp) {
    return pullAction(Fetch::Found, compareProgress(local, lp, remote, rp), false);
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

// ---- reading back, then sleeping: the machine's own older record never pulls it away ---------------------------

namespace {
const std::string kProgress20 = "/body/DocFragment[21]/body/p[3]/text().0";
const std::string kProgress3 = "/body/DocFragment[4]/body/p[1]/text().0";
}  // namespace

TEST(AutoSyncReread, ReadingBackThenSleepingPushesTheNewPlaceAndTheNextWakeKeepsIt) {
  // The server holds chapter 20, the record this machine pushed the evening before.
  SyncMarkList marks;
  marks.put({"doc", recordToken(kProgress20, 0.60f), placeKey(20, 5)});
  // The reader went back to chapter 3 and closed the book there; sleep asks the server.
  const CrossPointPosition remote = resolved(20, 5);
  const CrossPointPosition local = resolved(3, 2);
  const auto cmp = compareProgress(local, 0.15f, remote, 0.60f);
  ASSERT_EQ(cmp, ProgressComparison::RemoteAhead);  // why the percentage rule alone dropped the debt
  const bool ours = marks.serverIsOurs("doc", kProgress20, 0.60f);
  const bool changed = marks.placeChanged("doc", placeKey(3, 2));
  EXPECT_TRUE(ours);
  EXPECT_TRUE(changed);
  EXPECT_EQ(pushAction(Fetch::Found, cmp, ours, changed), PushAction::Push);

  // The push went through: the mark follows the record now on the server.
  marks.put({"doc", recordToken(kProgress3, 0.15f), placeKey(3, 2)});
  // Next wake: the server still says chapter 3, which is where the book is. Nothing to pull.
  EXPECT_EQ(pullAction(Fetch::Found, compareProgress(local, 0.15f, resolved(3, 2), 0.15f),
                       marks.serverIsOurs("doc", kProgress3, 0.15f)),
            PullAction::Keep);
}

TEST(AutoSyncReread, AFailedPushLeavesTheOlderRecordAndTheWakeStillDoesNotPullIt) {
  // The sleep push could not reach the server: the mark still names chapter 20 and the book is at chapter 3.
  SyncMarkList marks;
  marks.put({"doc", recordToken(kProgress20, 0.60f), placeKey(20, 5)});
  const auto cmp = compareProgress(resolved(3, 2), 0.15f, resolved(20, 5), 0.60f);
  EXPECT_EQ(pullAction(Fetch::Found, cmp, marks.serverIsOurs("doc", kProgress20, 0.60f)), PullAction::Keep);
}

TEST(AutoSyncReread, AnotherMachineThatWroteFartherStillPullsAtWake) {
  SyncMarkList marks;
  marks.put({"doc", recordToken(kProgress3, 0.15f), placeKey(3, 2)});
  // The phone read on to chapter 20 and wrote that: not the record this machine left.
  const bool ours = marks.serverIsOurs("doc", kProgress20, 0.60f);
  EXPECT_FALSE(ours);
  EXPECT_EQ(pullAction(Fetch::Found, compareProgress(resolved(3, 2), 0.15f, resolved(20, 5), 0.60f), ours),
            PullAction::Apply);
}

TEST(AutoSyncReread, ABookWithoutAMarkKeepsTheOldRules) {
  const SyncMarkList marks;
  EXPECT_FALSE(marks.serverIsOurs("doc", kProgress20, 0.60f));
  EXPECT_TRUE(marks.placeChanged("doc", placeKey(3, 2)));
  const bool ours = false;
  EXPECT_EQ(pullAction(Fetch::Found, ProgressComparison::RemoteAhead, ours), PullAction::Apply);
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::RemoteAhead, ours, true), PushAction::DropDebt);
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::LocalAhead, ours, true), PushAction::Push);
}

TEST(AutoSyncReread, ANewerRecordFromAnotherMachineIsNotOverwrittenByAnOlderReading) {
  // Another machine wrote chapter 20 after this one's chapter 3; this machine read back to chapter 2.
  SyncMarkList marks;
  marks.put({"doc", recordToken(kProgress3, 0.15f), placeKey(3, 2)});
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::RemoteAhead,
                       marks.serverIsOurs("doc", kProgress20, 0.60f), marks.placeChanged("doc", placeKey(2, 0))),
            PushAction::DropDebt);
}

TEST(AutoSyncReread, OurRecordWithTheSamePlaceOrAnEqualPercentageNeedsNoPush) {
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::RemoteAhead, true, false), PushAction::DropDebt);
  EXPECT_EQ(pushAction(Fetch::Found, ProgressComparison::Synchronized, true, true), PushAction::DropDebt);
  EXPECT_EQ(pushAction(Fetch::Failed, ProgressComparison::RemoteAhead, true, true), PushAction::KeepDebt);
  EXPECT_EQ(pushAction(Fetch::NotFound, ProgressComparison::Unknown, true, true), PushAction::Push);
}

TEST(AutoSyncReread, RecordTokenIgnoresRoundingBelowFourDigitsAndSeesARealMove) {
  EXPECT_EQ(recordToken(kProgress20, 0.60000f), recordToken(kProgress20, 0.600004f));
  EXPECT_NE(recordToken(kProgress20, 0.6000f), recordToken(kProgress20, 0.6002f));
  EXPECT_NE(recordToken(kProgress20, 0.6f), recordToken(kProgress3, 0.6f));
  EXPECT_EQ(recordToken("", std::numeric_limits<float>::quiet_NaN()), recordToken("", 0.0f));
  EXPECT_NE(placeKey(3, 2), placeKey(2, 3));
}

TEST(SyncMarkList, KeepsTheNewestEightAndRoundTripsThroughTheFile) {
  SyncMarkList list;
  for (int i = 0; i < 10; ++i) list.put({"d" + std::to_string(i), static_cast<uint32_t>(i * 7 + 1), placeKey(i, i)});
  list.put({"d5", 99u, placeKey(1, 1)});  // replaces, newest
  ASSERT_EQ(list.size(), MAX_MARKS);
  EXPECT_EQ(list.find("d0"), nullptr);
  EXPECT_EQ(list.find("d1"), nullptr);
  ASSERT_NE(list.find("d5"), nullptr);
  EXPECT_EQ(list.find("d5")->token, 99u);
  std::string text;
  {
    JsonDocument doc;
    list.toJson(doc);
    serializeJson(doc, text);
  }
  EXPECT_LT(text.size(), 512u);  // far under the 2 KB the firmware reads at most
  JsonDocument back;
  ASSERT_FALSE(deserializeJson(back, text));
  SyncMarkList loaded;
  ASSERT_TRUE(loaded.fromJson(back));
  ASSERT_EQ(loaded.size(), MAX_MARKS);
  ASSERT_NE(loaded.find("d9"), nullptr);
  EXPECT_EQ(loaded.find("d9")->token, 9u * 7 + 1);
  EXPECT_EQ(loaded.find("d9")->place, placeKey(9, 9));
}

TEST(SyncMarkList, WrongShapeAndBadEntriesAreRefused) {
  for (const char* text : {"[]", "{}", R"({"marks":3})", "42"}) {
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, text)) << text;
    SyncMarkList list;
    list.put({"stale", 1u, 1u});
    EXPECT_FALSE(list.fromJson(doc)) << text;
    EXPECT_TRUE(list.empty()) << text;
  }
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"v":1,"marks":[{"d":"","t":1,"p":1},{"t":1,"p":1},{"d":"x","p":1},{"d":"y","t":1},{"d":"ok","t":4,"p":5}]})"));
  SyncMarkList list;
  ASSERT_TRUE(list.fromJson(doc));
  ASSERT_EQ(list.size(), 1u);
  EXPECT_NE(list.find("ok"), nullptr);
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

TEST(WakePullBook, OnlyAnEpubIsPulled) {
  EXPECT_TRUE(isEpubPath("/Books/a.epub"));
  EXPECT_TRUE(isEpubPath("/Books/B.EPUB"));
  EXPECT_TRUE(isEpubPath("/Books/c.Epub"));
  EXPECT_FALSE(isEpubPath("/Books/notes.txt"));
  EXPECT_FALSE(isEpubPath("/Books/scan.xtc"));
  EXPECT_FALSE(isEpubPath("/Books/scan.xtch"));
  EXPECT_FALSE(isEpubPath("/Books/epub"));
  EXPECT_FALSE(isEpubPath("/Books/a.epub.txt"));
  EXPECT_FALSE(isEpubPath(".epu"));
  EXPECT_FALSE(isEpubPath(""));
}

TEST(AutoSyncDebt, ADebtIsDroppedOnlyWhenTheCardAnswersAndTheBookIsNotThere) {
  EXPECT_EQ(checkBook(true, false, false), BookCheck::Gone);
  EXPECT_EQ(checkBook(true, true, true), BookCheck::Ready);
  // A card that fails to answer says nothing about the book: the debt stays for the next sleep.
  EXPECT_EQ(checkBook(false, false, false), BookCheck::Unreadable);
  EXPECT_EQ(checkBook(false, true, true), BookCheck::Unreadable);
  // The book is there but cannot be hashed (a read error): same.
  EXPECT_EQ(checkBook(true, true, false), BookCheck::Unreadable);
}

TEST(WakePullSleep, AMeasuringBuildCanLowerTheRuleAndTheDefaultStaysThirtyMinutes) {
  const uint32_t at = 1791700000;
  EXPECT_TRUE(sleptLongEnough(at, at + 10, true, 5));
  EXPECT_FALSE(sleptLongEnough(at, at + 4, true, 5));
  EXPECT_FALSE(sleptLongEnough(at, at + 10, false, 5));  // a clock that is not trusted still counts as short
  EXPECT_FALSE(sleptLongEnough(at, at + PULL_MIN_SLEEP_SECONDS - 1, true));
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
// ---- the pull owed to the first book opened from Home ----------------------------------------------------------

namespace {
// One wake and the opens that follow, the way KOReaderAutoSync.cpp drives the two functions.
struct HomeSession {
  PullDebt debt = PullDebt::None;
  int restarts = 0;
  int pulls = 0;

  void start(const bool quiet, const bool onHome, const bool slept, const bool wanted = true) {
    debt = debtAfterStart(debt, quiet, onHome, slept, wanted);
  }
  // A book opened from Home. true when the open went through the restart and pulled on its fresh start.
  bool open(const bool epub, const bool wanted = true) {
    OpenPlan plan = planOpen(debt, epub, wanted);
    debt = plan.debtAfter;
    if (plan.step != OpenStep::RestartForPull) return false;
    ++restarts;
    start(true, false, false);  // the quiet restart keeps what was armed
    plan = planOpen(debt, epub, wanted);
    debt = plan.debtAfter;
    if (plan.step == OpenStep::PullThenOpen) ++pulls;
    return plan.step == OpenStep::PullThenOpen;
  }
};
}  // namespace

TEST(AutoSyncOpenPull, LongSleepOnHomePullsOnTheFirstEpubOnly) {
  HomeSession s;
  s.start(false, true, true);
  EXPECT_EQ(s.debt, PullDebt::Owed);
  EXPECT_TRUE(s.open(true));
  EXPECT_EQ(s.debt, PullDebt::None);
  EXPECT_FALSE(s.open(true));  // the second book pulls nothing
  EXPECT_EQ(s.restarts, 1);
  EXPECT_EQ(s.pulls, 1);
}

TEST(AutoSyncOpenPull, ShortSleepSwitchOffOrABookWakeOwesNothing) {
  HomeSession shortSleep;
  shortSleep.start(false, true, false);
  EXPECT_EQ(shortSleep.debt, PullDebt::None);
  EXPECT_FALSE(shortSleep.open(true));

  HomeSession off;  // switch off or no account
  off.start(false, true, true, false);
  EXPECT_EQ(off.debt, PullDebt::None);
  EXPECT_FALSE(off.open(true));
  EXPECT_EQ(off.restarts, 0);

  HomeSession bookWake;  // the wake went into the book: the wake pull has its own path
  bookWake.start(false, false, true);
  EXPECT_EQ(bookWake.debt, PullDebt::None);
}

TEST(AutoSyncOpenPull, ATextBookLeavesItOwedForTheNextEpub) {
  HomeSession s;
  s.start(false, true, true);
  EXPECT_FALSE(s.open(false));
  EXPECT_EQ(s.debt, PullDebt::Owed);
  EXPECT_EQ(s.restarts, 0);
  EXPECT_TRUE(s.open(true));
}

TEST(AutoSyncOpenPull, TheSwitchGoingOffBeforeTheOpenForgetsTheDebt) {
  HomeSession s;
  s.start(false, true, true);
  EXPECT_FALSE(s.open(true, false));
  EXPECT_EQ(s.debt, PullDebt::None);
  EXPECT_EQ(s.restarts, 0);
  EXPECT_FALSE(s.open(true));
}

TEST(AutoSyncOpenPull, AQuietRestartToHomeKeepsTheDebtAndTheNextSleepDecidesAgain) {
  HomeSession s;
  s.start(false, true, true);
  s.start(true, false, false);  // a Wi-Fi screen restarting to Home
  EXPECT_EQ(s.debt, PullDebt::Owed);
  s.start(false, true, false);  // the next wake after a short sleep
  EXPECT_EQ(s.debt, PullDebt::None);
  EXPECT_FALSE(s.open(true));
}

TEST(AutoSyncOpenPull, ArmedIsSpentByTheStartThatPullsAndLeftoverRtcOwesNothing) {
  EXPECT_EQ(planOpen(PullDebt::Armed, true, true).step, OpenStep::PullThenOpen);
  EXPECT_EQ(planOpen(PullDebt::Armed, true, true).debtAfter, PullDebt::None);
  EXPECT_EQ(planOpen(PullDebt::Armed, true, false).step, OpenStep::Open);
  EXPECT_EQ(pullDebtFromStored(0), PullDebt::None);
  EXPECT_EQ(pullDebtFromStored(0xDEADBEEF), PullDebt::None);
  EXPECT_EQ(pullDebtFromStored(static_cast<uint32_t>(PullDebt::Owed)), PullDebt::Owed);
  EXPECT_EQ(pullDebtFromStored(static_cast<uint32_t>(PullDebt::Armed)), PullDebt::Armed);
  // a power loss is a cold start: whatever RTC held, the start decides again
  EXPECT_EQ(debtAfterStart(PullDebt::Armed, false, false, false, true), PullDebt::None);
}

TEST(AutoSyncOpenPull, OwedPlanConsumesDebtBeforeASkippedNetworkPull) {
  const OpenPlan restart = planOpen(PullDebt::Owed, true, true);
  EXPECT_EQ(restart.step, OpenStep::RestartForPull);
  EXPECT_EQ(restart.debtAfter, PullDebt::Armed);
  const OpenPlan plan = planOpen(restart.debtAfter, true, true);
  EXPECT_EQ(plan.step, OpenStep::PullThenOpen);
  EXPECT_EQ(plan.debtAfter, PullDebt::None);
  // A skipped network round leaves the already-consumed plan at None. The next open is ordinary.
  EXPECT_EQ(planOpen(plan.debtAfter, true, true).step, OpenStep::Open);
  EXPECT_EQ(planOpen(plan.debtAfter, true, true).debtAfter, PullDebt::None);
}

TEST(AutoSyncOpenPull, OwedPlanPullsWithoutHeapRestartWhenBleIsOff) {
  const OpenPlan plan = planOpen(PullDebt::Owed, true, true, false);
  EXPECT_EQ(plan.step, OpenStep::PullThenOpen);
  EXPECT_EQ(plan.debtAfter, PullDebt::None);
}
