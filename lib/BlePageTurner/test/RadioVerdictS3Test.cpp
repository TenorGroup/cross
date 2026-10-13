// The S3 profile of radioVerdict: the numbers measured on an X4 Pro on 13/10/2026 with NimBLE's host
// allocated from PSRAM, and the start at Home. The default profile (X3) keeps its numbers.

#include <gtest/gtest.h>

#include "RadioPolicy.h"

namespace bleturner {
namespace {

// Measured on the unit (probe log t2/do-may/261013b): Home before and after the stack came up.
constexpr Heap kHomeBefore{124752, 81908};
constexpr Heap kHomeAfter{78244, 36852};
// The same stack with its host in internal RAM left 31,732 largest: rolled back.
constexpr Heap kInternalAfter{56604, 31732};

RadioInputs home(const Phase phase) {
  RadioInputs in{};
  in.phase = phase;
  in.enabled = true;
  in.where = Where::Home;
  in.pageShown = true;
  in.hasBond = true;
  in.heap = kHomeBefore;
  return in;
}

TEST(RadioVerdictS3Test, TheNumbersAreTheMeasuredOnesPlusMargin) {
  EXPECT_EQ(kS3StackBytes, 46508u + 4096u);
  EXPECT_EQ(kS3Profile.stackBytes, kS3StackBytes);
  EXPECT_EQ(kS3Profile.startFreeBytes, kS3StackBytes + kMinimumLargestBlockBytes);
  EXPECT_EQ(kS3Profile.minimumLargest, kMinimumLargestBlockBytes);
  // The stack takes 45,056 B from the largest block; a start at Home must leave the reader its block.
  EXPECT_EQ(kS3Profile.homeStartLargest, kMinimumLargestBlockBytes + 45056u + 2048u);
  EXPECT_LE(kS3Profile.homeStartLargest, kHomeBefore.largestBlock) << "the unit's Home passes";
  EXPECT_LE(kS3Profile.startFreeBytes, kHomeBefore.freeBytes);
}

TEST(RadioVerdictS3Test, TheDefaultProfileIsStillTheX3One) {
  EXPECT_EQ(kStackBytes, 49152u);
  EXPECT_EQ(kStartFreeBytes, 81920u);
  EXPECT_EQ(kX3Profile.stackBytes, kStackBytes);
  EXPECT_EQ(kX3Profile.startFreeBytes, kStartFreeBytes);
  EXPECT_EQ(kX3Profile.homeStartLargest, 0u);
  EXPECT_EQ(&activeProfile(), &kX3Profile) << "this test binary is built without FREEINK_BLE_STACK_IN_PSRAM";
}

TEST(RadioVerdictS3Test, HomeMayAskOnlyOnS3WithABondOnAShownPage) {
  EXPECT_EQ(radioVerdict(home(Phase::Idle), kS3Profile), Why::Ok);
  EXPECT_EQ(radioVerdict(home(Phase::Idle), kX3Profile), Why::NotReading);
  EXPECT_EQ(radioVerdict(home(Phase::Idle)), Why::NotReading);
  RadioInputs in = home(Phase::Idle);
  in.hasBond = false;
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::NotReading);
  in = home(Phase::Idle);
  in.pageShown = false;
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::PageNotShown);
  in = home(Phase::Idle);
  in.enabled = false;
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::Off);
  in = home(Phase::Idle);
  in.storageBusy = true;
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::StorageBusy);
  in = home(Phase::Idle);
  in.idleStopped = true;
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::IdleNoLink);
}

TEST(RadioVerdictS3Test, StartingAtHomeNeedsTheBlockTheReaderWillStillWant) {
  EXPECT_EQ(radioVerdict(home(Phase::BeforeStart), kS3Profile), Why::Ok);
  RadioInputs in = home(Phase::BeforeStart);
  in.heap.largestBlock = kS3Profile.homeStartLargest - 1;
  // Not HeapInPieces: that verdict counts toward a restart into the book, which makes no sense at Home.
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::HeapLow);
  in = home(Phase::BeforeStart);
  in.heap.freeBytes = kS3Profile.startFreeBytes - 1;
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::HeapLow);
}

TEST(RadioVerdictS3Test, InTheBookTheSamePieceRulesApplyWithTheS3Numbers) {
  RadioInputs in = home(Phase::BeforeStart);
  in.where = Where::Reader;
  in.heap = {kS3Profile.startFreeBytes, kMinimumLargestBlockBytes};
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::Ok);
  in.heap.largestBlock = kMinimumLargestBlockBytes - 1;
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::HeapInPieces) << "the silent restart into the book still applies";
  in.heap = {kS3Profile.startFreeBytes - 1, kMinimumLargestBlockBytes};
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::HeapInPieces);
  in.heap = {kMinimumFreeBytes - 1, kMinimumLargestBlockBytes};
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::HeapLow);
}

TEST(RadioVerdictS3Test, TheStackIsKeptOrRolledBackOnTheLargestBlockAfterIt) {
  RadioInputs in = home(Phase::JustStarted);
  in.heap = kHomeAfter;
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::Ok) << "PSRAM host: 36,852 left";
  in.heap = kInternalAfter;
  EXPECT_EQ(radioVerdict(in, kS3Profile), Why::HeapInPieces) << "internal host: 31,732 left";
}

}  // namespace
}  // namespace bleturner
