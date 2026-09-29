#include <gtest/gtest.h>

#include "activities/reader/ChapterBuildGate.h"

namespace {
bleturner::Status radio() { return bleturner::Status{}; }
}  // namespace

TEST(ChapterBuildGate, RunsWithTheRadioOff) {
  EXPECT_TRUE(chapterBuildMayRun(radio()));
  auto idle = radio();
  idle.idleStopped = true;  // stopped for this build, or for idleness
  EXPECT_TRUE(chapterBuildMayRun(idle));
  auto refused = radio();
  refused.readerDeferred = true;  // refused for memory: it holds nothing
  EXPECT_TRUE(chapterBuildMayRun(refused));
}

// The radio did not stop within its budget (StillUp), or a start was still settling after it
// (NotHeld): it holds or is taking the heap the build needs.
TEST(ChapterBuildGate, RefusedWhileTheRadioHoldsOrTakesTheHeap) {
  auto up = radio();
  up.running = true;
  up.connected = true;
  EXPECT_FALSE(chapterBuildMayRun(up));
  auto stopping = radio();
  stopping.stopping = true;
  EXPECT_FALSE(chapterBuildMayRun(stopping));
  auto starting = radio();
  starting.starting = true;
  EXPECT_FALSE(chapterBuildMayRun(starting));
}
