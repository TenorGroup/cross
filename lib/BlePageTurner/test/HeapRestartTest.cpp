// When a heap in pieces keeps the radio off, and the one restart it may cost.
// Moved from the app's ble_heap_restart suite.

#include <gtest/gtest.h>

#include "RadioPolicy.h"

namespace bleturner {
namespace {

// X3, 27/09/2026: the heap the radio was refused with, in the book and on Home alike.
constexpr size_t kFree = 87308;
constexpr size_t kLargest = 23540;

TEST(BleHeapRestartDecisionTest, ThirdFragmentedRefusalInARowRestarts) {
  EXPECT_FALSE(shouldRestart(kFree, kLargest, 1, false));
  EXPECT_FALSE(shouldRestart(kFree, kLargest, 2, false));
  EXPECT_TRUE(shouldRestart(kFree, kLargest, 3, false));
  EXPECT_TRUE(shouldRestart(kFree, kLargest, 4, false));
}

TEST(BleHeapRestartDecisionTest, HeapThatIsShortInTotalDoesNotRestart) {
  // A restart cannot give back bytes the book really uses.
  EXPECT_FALSE(shouldRestart(kMinimumFreeBytes - 1, kLargest, 3, false));
  EXPECT_TRUE(shouldRestart(kMinimumFreeBytes, kLargest, 3, false));
}

TEST(BleHeapRestartDecisionTest, WholeLargestBlockDoesNotRestart) {
  EXPECT_FALSE(shouldRestart(kFree, kMinimumLargestBlockBytes, 3, false));
  EXPECT_TRUE(shouldRestart(kFree, kMinimumLargestBlockBytes - 1, 3, false));
}

TEST(BleHeapRestartDecisionTest, OneRestartUntilTheRadioComesUp) {
  EXPECT_FALSE(shouldRestart(kFree, kLargest, 3, true));
  EXPECT_FALSE(shouldRestart(kFree, kLargest, 200, true));
}

TEST(BleHeapRestartTrackerTest, ShortHeapBreaksTheStreak) {
  Memo memo{0};
  Tracker tracker{memo};
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(kMinimumFreeBytes - 1, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_TRUE(tracker.refused(kFree, kLargest));
}

TEST(BleHeapRestartTrackerTest, PassedHeapCheckBreaksTheStreak) {
  // A post-init rollback follows a passed check: it is not a fragmented refusal.
  Memo memo{0};
  Tracker tracker{memo};
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  tracker.passed();
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_TRUE(tracker.refused(kFree, kLargest));
}

// The RTC memo is the only state a restart keeps; a new Tracker over the same memo is the next boot.
TEST(BleHeapRestartTrackerTest, OneRestartSurvivesARestartUntilTheRadioComesUp) {
  Memo memo{0};
  {
    Tracker boot1{memo};
    EXPECT_FALSE(boot1.refused(kFree, kLargest));
    EXPECT_FALSE(boot1.refused(kFree, kLargest));
    ASSERT_TRUE(boot1.refused(kFree, kLargest));
    boot1.restarting();
  }
  {
    // Still in pieces after the restart: no second restart, however long it stays so.
    Tracker boot2{memo};
    for (int i = 0; i < 300; ++i) EXPECT_FALSE(boot2.refused(kFree, kLargest)) << i;
  }
  {
    Tracker boot3{memo};
    EXPECT_FALSE(boot3.refused(kFree, kLargest));
    boot3.radioUp();
    // The radio came up: the next time the heap breaks up, the safety net is there again.
    EXPECT_FALSE(boot3.refused(kFree, kLargest));
    EXPECT_FALSE(boot3.refused(kFree, kLargest));
    EXPECT_TRUE(boot3.refused(kFree, kLargest));
  }
}

// Asked in one book, then Home and another book whose heap passes: a later stack failure there
// is not fragmentation and must not restart the device.
TEST(BleHeapRestartTrackerTest, PassedHeapCheckDropsAnEarlierRequest) {
  Memo memo{0};
  Tracker tracker{memo};
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  ASSERT_TRUE(tracker.refused(kFree, kLargest));
  EXPECT_TRUE(tracker.wanted.load());
  tracker.passed();
  EXPECT_FALSE(tracker.wanted.load());
}

// Asked in one book, then another book refused for a heap short in total: a restart cannot give
// back bytes that book really uses, so the request is dropped.
TEST(BleHeapRestartTrackerTest, HeapShortInTotalDropsAnEarlierRequest) {
  Memo memo{0};
  Tracker tracker{memo};
  for (int i = 0; i < 3; ++i) tracker.refused(kFree, kLargest);
  ASSERT_TRUE(tracker.wanted.load());
  EXPECT_FALSE(tracker.refused(kMinimumFreeBytes - 1, kLargest));
  EXPECT_FALSE(tracker.wanted.load());
}

TEST(BleHeapRestartTrackerTest, RestartingClearsTheRequest) {
  Memo memo{0};
  Tracker tracker{memo};
  for (int i = 0; i < 3; ++i) tracker.refused(kFree, kLargest);
  ASSERT_TRUE(tracker.wanted.load());
  tracker.restarting();
  EXPECT_FALSE(tracker.wanted.load());
}

TEST(BleHeapRestartTrackerTest, ColdBootGarbageInTheMemoIsNotARestart) {
  for (const uint32_t garbage : {0u, 0xFFFFFFFFu, 0xDEADBEEFu, kRestartSpentMagic ^ 1u}) {
    Memo memo{garbage};
    Tracker tracker{memo};
    EXPECT_FALSE(tracker.refused(kFree, kLargest));
    EXPECT_FALSE(tracker.refused(kFree, kLargest));
    EXPECT_TRUE(tracker.refused(kFree, kLargest)) << garbage;
  }
}

}  // namespace
}  // namespace bleturner
