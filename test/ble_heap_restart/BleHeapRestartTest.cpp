#include <gtest/gtest.h>

#include "BleHeapRestart.h"

namespace {

// X3, 27/09/2026: the heap the radio was refused with, in the book and on Home alike.
constexpr size_t kFree = 87308;
constexpr size_t kLargest = 23540;

TEST(BleHeapRestartDecisionTest, ThirdFragmentedRefusalInARowRestarts) {
  EXPECT_FALSE(bleheap::shouldRestart(kFree, kLargest, 1, false));
  EXPECT_FALSE(bleheap::shouldRestart(kFree, kLargest, 2, false));
  EXPECT_TRUE(bleheap::shouldRestart(kFree, kLargest, 3, false));
  EXPECT_TRUE(bleheap::shouldRestart(kFree, kLargest, 4, false));
}

TEST(BleHeapRestartDecisionTest, HeapThatIsShortInTotalDoesNotRestart) {
  // A restart cannot give back bytes the book really uses.
  EXPECT_FALSE(bleheap::shouldRestart(bleheap::kMinimumFreeBytes - 1, kLargest, 3, false));
  EXPECT_TRUE(bleheap::shouldRestart(bleheap::kMinimumFreeBytes, kLargest, 3, false));
}

TEST(BleHeapRestartDecisionTest, WholeLargestBlockDoesNotRestart) {
  EXPECT_FALSE(bleheap::shouldRestart(kFree, bleheap::kMinimumLargestBlockBytes, 3, false));
  EXPECT_TRUE(bleheap::shouldRestart(kFree, bleheap::kMinimumLargestBlockBytes - 1, 3, false));
}

TEST(BleHeapRestartDecisionTest, OneRestartUntilTheRadioComesUp) {
  EXPECT_FALSE(bleheap::shouldRestart(kFree, kLargest, 3, true));
  EXPECT_FALSE(bleheap::shouldRestart(kFree, kLargest, 200, true));
}

TEST(BleHeapRestartTrackerTest, ShortHeapBreaksTheStreak) {
  bleheap::Memo memo{0};
  bleheap::Tracker tracker{memo};
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(bleheap::kMinimumFreeBytes - 1, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_TRUE(tracker.refused(kFree, kLargest));
}

TEST(BleHeapRestartTrackerTest, PassedHeapCheckBreaksTheStreak) {
  // A post-init rollback follows a passed check: it is not a fragmented refusal.
  bleheap::Memo memo{0};
  bleheap::Tracker tracker{memo};
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  tracker.passed();
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_FALSE(tracker.refused(kFree, kLargest));
  EXPECT_TRUE(tracker.refused(kFree, kLargest));
}

// The RTC memo is the only state a restart keeps; a new Tracker over the same memo is the next boot.
TEST(BleHeapRestartTrackerTest, OneRestartSurvivesARestartUntilTheRadioComesUp) {
  bleheap::Memo memo{0};
  {
    bleheap::Tracker boot1{memo};
    EXPECT_FALSE(boot1.refused(kFree, kLargest));
    EXPECT_FALSE(boot1.refused(kFree, kLargest));
    ASSERT_TRUE(boot1.refused(kFree, kLargest));
    boot1.restarting();
  }
  {
    // Still in pieces after the restart: no second restart, however long it stays so.
    bleheap::Tracker boot2{memo};
    for (int i = 0; i < 300; ++i) EXPECT_FALSE(boot2.refused(kFree, kLargest)) << i;
  }
  {
    bleheap::Tracker boot3{memo};
    EXPECT_FALSE(boot3.refused(kFree, kLargest));
    boot3.radioUp();
    // The radio came up: the next time the heap breaks up, the safety net is there again.
    EXPECT_FALSE(boot3.refused(kFree, kLargest));
    EXPECT_FALSE(boot3.refused(kFree, kLargest));
    EXPECT_TRUE(boot3.refused(kFree, kLargest));
  }
}

TEST(BleHeapRestartTrackerTest, ColdBootGarbageInTheMemoIsNotARestart) {
  for (const uint32_t garbage : {0u, 0xFFFFFFFFu, 0xDEADBEEFu, bleheap::kRestartSpentMagic ^ 1u}) {
    bleheap::Memo memo{garbage};
    bleheap::Tracker tracker{memo};
    EXPECT_FALSE(tracker.refused(kFree, kLargest));
    EXPECT_FALSE(tracker.refused(kFree, kLargest));
    EXPECT_TRUE(tracker.refused(kFree, kLargest)) << garbage;
  }
}

}  // namespace
