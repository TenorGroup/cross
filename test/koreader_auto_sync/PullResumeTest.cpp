#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <string>

#include "KOReaderPullResume.h"

using namespace kosyncauto;

namespace {
constexpr size_t kRadioStartFreeBytes = 81920;
constexpr size_t kRadioLargestBlockBytes = 32768;
}  // namespace

TEST(PullResumeRestart, ExactRadioHeapBoundaryNeedsNoRestart) {
  EXPECT_FALSE(restartAfterPull(true, true, true, false, kRadioStartFreeBytes,
                                kRadioLargestBlockBytes, kRadioStartFreeBytes,
                                kRadioLargestBlockBytes));
  EXPECT_TRUE(restartAfterPull(true, true, true, false, kRadioStartFreeBytes - 1,
                               kRadioLargestBlockBytes, kRadioStartFreeBytes,
                               kRadioLargestBlockBytes));
  EXPECT_TRUE(restartAfterPull(true, true, true, false, kRadioStartFreeBytes,
                               kRadioLargestBlockBytes - 1, kRadioStartFreeBytes,
                               kRadioLargestBlockBytes));
}

TEST(PullResumeRestart, DisabledUnusedFailedOrSpentPullNeverRestarts) {
  constexpr size_t lowFree = kRadioStartFreeBytes - 1;
  constexpr size_t lowLargest = kRadioLargestBlockBytes - 1;
  EXPECT_FALSE(restartAfterPull(false, true, true, false, lowFree, lowLargest,
                                kRadioStartFreeBytes, kRadioLargestBlockBytes));
  EXPECT_FALSE(restartAfterPull(true, false, true, false, lowFree, lowLargest,
                                kRadioStartFreeBytes, kRadioLargestBlockBytes));
  EXPECT_FALSE(restartAfterPull(true, true, false, false, lowFree, lowLargest,
                                kRadioStartFreeBytes, kRadioLargestBlockBytes));
  EXPECT_FALSE(restartAfterPull(true, true, true, true, lowFree, lowLargest,
                                kRadioStartFreeBytes, kRadioLargestBlockBytes));
}

TEST(PullResumeRestart, LowHeapPolicyRequestsOneRestartUntilMarkedSpent) {
  bool spent = false;
  EXPECT_TRUE(restartAfterPull(true, true, true, spent, kRadioStartFreeBytes,
                               kRadioLargestBlockBytes - 1, kRadioStartFreeBytes,
                               kRadioLargestBlockBytes));
  spent = true;
  EXPECT_FALSE(restartAfterPull(true, true, true, spent, 0, 0,
                                kRadioStartFreeBytes, kRadioLargestBlockBytes));
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
  const std::string longest = "/" + std::string(sizeof(resume.path) - 2, 'p');
  ASSERT_TRUE(keepPullPath(resume, longest, PULL_ARMED_MAGIC));
  EXPECT_EQ(std::strlen(resume.path), longest.size());
  EXPECT_TRUE(validPullResume(resume));

  resume.hasMark = true;
  EXPECT_FALSE(keepPullPath(resume, std::string(sizeof(resume.path), 'p'), PULL_ARMED_MAGIC));
  EXPECT_EQ(resume.magic, 0u);
  EXPECT_EQ(resume.path[0], '\0');
  EXPECT_FALSE(resume.hasMark);

  resume.magic = PULL_DONE_MAGIC;
  resume.path[0] = 'x';
  EXPECT_FALSE(keepPullPath(resume, "", PULL_ARMED_MAGIC));
  EXPECT_EQ(resume.magic, 0u);
  EXPECT_EQ(resume.path[0], '\0');
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
