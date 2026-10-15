#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <string>

#include "KOReaderPullResume.h"
#include "RadioPolicy.h"

using namespace kosyncauto;

namespace {
constexpr size_t kRadioStartFreeBytes = bleturner::kX3Profile.startFreeBytes;
constexpr size_t kRadioLargestBlockBytes = bleturner::kX3Profile.minimumLargest;

bleturner::RadioInputs readerBeforeStart(const size_t freeBytes, const size_t largest) {
  bleturner::RadioInputs in{};
  in.phase = bleturner::Phase::BeforeStart;
  in.where = bleturner::Where::Reader;
  in.heap = {freeBytes, largest};
  return in;
}
}  // namespace

static_assert(kPullReaderReserveBytes == 20480);
static_assert(PULL_JOIN_MS == 9500);

TEST(PullResumeRestart, UsesThePageTurnerReaderVerdictAtTheX3Boundary) {
  EXPECT_EQ(bleturner::radioVerdict(readerBeforeStart(kRadioStartFreeBytes, kRadioLargestBlockBytes),
                                    bleturner::kX3Profile),
            bleturner::Why::Ok);
  EXPECT_EQ(bleturner::radioVerdict(readerBeforeStart(kRadioStartFreeBytes - 1, kRadioLargestBlockBytes),
                                    bleturner::kX3Profile),
            bleturner::Why::HeapInPieces);
  EXPECT_EQ(bleturner::radioVerdict(readerBeforeStart(kRadioStartFreeBytes, kRadioLargestBlockBytes - 1),
                                    bleturner::kX3Profile),
            bleturner::Why::HeapInPieces);
  EXPECT_EQ(bleturner::radioVerdict(readerBeforeStart(bleturner::kMinimumFreeBytes - 1,
                                                      kRadioLargestBlockBytes - 1),
                                    bleturner::kX3Profile),
            bleturner::Why::HeapLow);
}

TEST(PullResumeRestart, ExactRadioHeapBoundaryNeedsNoRestart) {
  EXPECT_FALSE(restartAfterPull(true, true, true, false,
                                kRadioStartFreeBytes + kPullReaderReserveBytes,
                                kRadioLargestBlockBytes + kPullReaderReserveBytes));
  EXPECT_TRUE(restartAfterPull(true, true, true, false,
                               kRadioStartFreeBytes + kPullReaderReserveBytes - 1,
                               kRadioLargestBlockBytes + kPullReaderReserveBytes));
  EXPECT_TRUE(restartAfterPull(true, true, true, false,
                               kRadioStartFreeBytes + kPullReaderReserveBytes,
                               kRadioLargestBlockBytes + kPullReaderReserveBytes - 1));
}

TEST(PullResumeRestart, DisabledUnusedFailedOrSpentPullNeverRestarts) {
  constexpr size_t lowFree = kRadioStartFreeBytes - 1;
  constexpr size_t lowLargest = kRadioLargestBlockBytes - 1;
  EXPECT_FALSE(restartAfterPull(false, true, true, false, lowFree, lowLargest));
  EXPECT_FALSE(restartAfterPull(true, false, true, false, lowFree, lowLargest));
  EXPECT_FALSE(restartAfterPull(true, true, false, false, lowFree, lowLargest));
  EXPECT_FALSE(restartAfterPull(true, true, true, true, lowFree, lowLargest));
}

TEST(PullResumeRestart, LowHeapPolicyRequestsOneRestartUntilMarkedSpent) {
  bool spent = false;
  EXPECT_TRUE(restartAfterPull(true, true, true, spent, kRadioStartFreeBytes,
                               kRadioLargestBlockBytes - 1));
  spent = true;
  EXPECT_FALSE(restartAfterPull(true, true, true, spent, 0, 0));
}

TEST(PullResumeRestart, MeasuredAfterPullHeapReservesTheReaderBeforeTheBleVerdict) {
  EXPECT_TRUE(restartAfterPull(true, true, true, false, 80704, 38900));
  EXPECT_TRUE(restartAfterPull(true, true, true, false, 67492, 21492));
  EXPECT_TRUE(restartAfterPull(true, true, true, false, 90000, 38900));
  EXPECT_FALSE(restartAfterPull(true, true, true, false, bleturner::kMinimumFreeBytes - 1, 60000));
}

TEST(PullResumeRecord, PositionAndMarkSurviveAnRtcCopy) {
  PullResume saved{};
  ASSERT_TRUE(keepPullPath(saved, "/Books/Back.epub", PULL_ARMED_MAGIC));
  saved.fromSpine = 7;
  saved.fromPage = 13;
  saved.toSpine = 8;
  saved.toPage = 2;
  saved.fromOffset = 4567;
  saved.token = 91;
  saved.place = 23;
  saved.hasFromOffset = true;
  saved.hasChange = true;
  saved.hasMark = true;

  const PullResume loaded = saved;
  ASSERT_TRUE(validPullResume(loaded));
  EXPECT_STREQ(loaded.path, "/Books/Back.epub");
  EXPECT_EQ(loaded.fromSpine, 7);
  EXPECT_EQ(loaded.fromPage, 13);
  EXPECT_EQ(loaded.toSpine, 8);
  EXPECT_EQ(loaded.toPage, 2);
  EXPECT_EQ(loaded.fromOffset, 4567u);
  EXPECT_EQ(loaded.token, 91u);
  EXPECT_EQ(loaded.place, 23u);
  EXPECT_TRUE(loaded.hasFromOffset);
  EXPECT_TRUE(loaded.hasChange);
  EXPECT_TRUE(loaded.hasMark);
}

TEST(PullResumeRecord, CorruptOrUnterminatedRecordsAreRejected) {
  PullResume good{};
  ASSERT_TRUE(keepPullPath(good, "/Books/Back.epub", PULL_ARMED_MAGIC));
  ASSERT_TRUE(validPullResume(good));

  PullResume corruptMagic = good;
  corruptMagic.magic ^= 0xFFFFFFFFu;
  EXPECT_FALSE(validPullResume(corruptMagic));

  PullResume unterminatedPath = good;
  std::memset(unterminatedPath.path, 'p', sizeof(unterminatedPath.path));
  EXPECT_FALSE(validPullResume(unterminatedPath));

  PullResume badFlag = good;
  badFlag.hasMark = 2;
  EXPECT_FALSE(validPullResume(badFlag));

  PullResume badOffsetFlag = good;
  badOffsetFlag.hasFromOffset = 2;
  EXPECT_FALSE(validPullResume(badOffsetFlag));

  PullResume badChangeFlag = good;
  badChangeFlag.hasChange = 255;
  EXPECT_FALSE(validPullResume(badChangeFlag));

  PullResume badMarkFlag = good;
  badMarkFlag.hasMark = 2;
  EXPECT_FALSE(validPullResume(badMarkFlag));
}

TEST(PullResumeRecord, PathCopyUsesTheWholeSafeRangeAndClearsFailures) {
  PullResume resume{};
  const auto pathOfLength = [](const size_t length) {
    return "/" + std::string(length - 1, 'p');
  };
  const std::string longest = pathOfLength(175);
  ASSERT_TRUE(keepPullPath(resume, longest, PULL_ARMED_MAGIC));
  EXPECT_EQ(std::strlen(resume.path), longest.size());
  EXPECT_TRUE(validPullResume(resume));

  resume.hasMark = true;
  EXPECT_FALSE(keepPullPath(resume, pathOfLength(176), PULL_ARMED_MAGIC));
  EXPECT_EQ(resume.magic, 0u);
  EXPECT_EQ(resume.path[0], '\0');
  EXPECT_FALSE(resume.hasMark);

  resume.magic = PULL_DONE_MAGIC;
  resume.path[0] = 'x';
  EXPECT_FALSE(keepPullPath(resume, pathOfLength(255), PULL_ARMED_MAGIC));
  EXPECT_EQ(resume.magic, 0u);
  EXPECT_EQ(resume.path[0], '\0');
}

TEST(PullResumeRecord, LongWakePathUsesTheAppStatePathAcrossRestart) {
  const auto pathOfLength = [](const size_t length) {
    return "/" + std::string(length - 6, 'p') + ".epub";
  };
  for (const size_t length : {size_t{176}, size_t{255}}) {
    PullResume resume{};
    ASSERT_TRUE(keepPullPath(resume, pathOfLength(length), PULL_DONE_MAGIC, true)) << length;
    EXPECT_TRUE(validPullResume(resume)) << length;
    EXPECT_EQ(resume.magic, PULL_DONE_MAGIC) << length;
    EXPECT_STREQ(resume.path, "/") << length;
    EXPECT_EQ(resume.useStoredPath, 1) << length;
  }
}

TEST(PullResumeRestart, PathMustBeSavedBeforeTheHeapRestartDecision) {
  const auto pathOfLength = [](const size_t length) { return "/" + std::string(length - 1, 'p'); };
  for (const size_t length : {size_t{175}, size_t{176}, size_t{255}}) {
    PullResume saved{};
    const bool pathSaved = keepPullPath(saved, pathOfLength(length), PULL_DONE_MAGIC);
    const bool restart = pathSaved &&
                         restartAfterPull(true, true, true, false,
                                          kRadioStartFreeBytes + kPullReaderReserveBytes,
                                          kRadioLargestBlockBytes + kPullReaderReserveBytes - 1);
    EXPECT_EQ(pathSaved, length == 175) << "path length " << length;
    EXPECT_EQ(restart, length == 175) << "path length " << length;
    EXPECT_EQ(saved.magic, length == 175 ? PULL_DONE_MAGIC : 0u) << "path length " << length;
  }
}

TEST(PullResumeRecord, DoneMarkerCannotBeConsumedAgain) {
  PullResume resume{};
  ASSERT_TRUE(keepPullPath(resume, "/Books/Back.epub", PULL_DONE_MAGIC));
  resume.token = 91;
  resume.place = 23;
  resume.hasMark = true;
  PullResume coldStart = resume;

  EXPECT_EQ(takePullMagic(resume, true), PULL_DONE_MAGIC);
  EXPECT_EQ(resume.magic, 0u);
  EXPECT_STREQ(resume.path, "/Books/Back.epub");
  EXPECT_EQ(resume.token, 91u);
  EXPECT_EQ(resume.place, 23u);
  EXPECT_TRUE(resume.hasMark);
  EXPECT_EQ(takePullMagic(resume, true), 0u);

  EXPECT_EQ(takePullMagic(coldStart, false), 0u);
  EXPECT_EQ(coldStart.magic, 0u);
}

TEST(PullResumeNetwork, RequestBudgetClampsAtSixSecondsAndExpiresAtTwelve) {
  EXPECT_EQ(pullRequestMs(0), 6000u);
  EXPECT_EQ(pullRequestMs(5999), 6000u);
  EXPECT_EQ(pullRequestMs(6000), 6000u);
  EXPECT_EQ(pullRequestMs(6001), 5999u);
  EXPECT_EQ(pullRequestMs(11999), 1u);
  EXPECT_EQ(pullRequestMs(12000), 0u);
  EXPECT_EQ(pullRequestMs(12001), 0u);
  EXPECT_FALSE(pullNetworkExpired(11999));
  EXPECT_TRUE(pullNetworkExpired(12000));
}

TEST(PullResumeNetwork, CallerElapsedTimeRemainsCorrectAcrossClockWrap) {
  constexpr uint32_t start = std::numeric_limits<uint32_t>::max() - 5000u;
  const uint32_t before = start + 11999u;
  const uint32_t deadline = start + 12000u;

  EXPECT_EQ(before - start, 11999u);
  EXPECT_EQ(pullRequestMs(before - start), 1u);
  EXPECT_FALSE(pullNetworkExpired(before - start));
  EXPECT_EQ(deadline - start, 12000u);
  EXPECT_EQ(pullRequestMs(deadline - start), 0u);
  EXPECT_TRUE(pullNetworkExpired(deadline - start));
}
