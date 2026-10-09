// "Wake into the book": the wake opens the book last opened while it is still in Recent and on
// the card, wherever the sleep started; otherwise Home.
#include <gtest/gtest.h>

#include "util/WakeBook.h"

namespace {
struct Book {
  std::string path;
};
const std::vector<Book> recents{{"/a.epub"}, {"/b.epub"}};
const auto onCard = [](const std::string&) { return true; };
const auto gone = [](const std::string&) { return false; };
}  // namespace

TEST(WakeBook, OpensTheLastBookWhenItIsInRecent) {
  EXPECT_EQ(wakebook::bookToOpen(true, true, "/b.epub", recents, onCard), "/b.epub");
}

TEST(WakeBook, SettingOffOrOtherBootGoesHome) {
  EXPECT_EQ(wakebook::bookToOpen(true, false, "/a.epub", recents, onCard), "");
  EXPECT_EQ(wakebook::bookToOpen(false, true, "/a.epub", recents, onCard), "");
  EXPECT_EQ(wakebook::bookToOpen(true, true, "", recents, onCard), "");
}

TEST(WakeBook, ABookTakenOffRecentIsNotReopened) {
  const std::vector<Book> left{{"/b.epub"}};
  EXPECT_EQ(wakebook::bookToOpen(true, true, "/a.epub", left, onCard), "");
}

TEST(WakeBook, ABookGoneFromTheCardGoesHome) {
  EXPECT_EQ(wakebook::bookToOpen(true, true, "/a.epub", recents, gone), "");
}

TEST(WakeBook, UglyBookOpensOnANormalColdBoot) {
  EXPECT_EQ(wakebook::bookToOpen(false, true, "/a.epub", recents, onCard, true), "/a.epub");
}

TEST(WakeBook, UnsafeColdBootsDoNotReopenTheBook) {
  EXPECT_EQ(wakebook::bookToOpen(false, true, "/a.epub", recents, onCard, false), "");
}

TEST(WakeBook, CrossColdBootStillGoesHomeWithWakeIntoBookOn) {
  EXPECT_EQ(wakebook::bookToOpen(false, true, "/a.epub", recents, onCard), "");
}

TEST(WakeBook, UglyColdBootUsesTheSameRecentAndCardChecks) {
  EXPECT_EQ(wakebook::bookToOpen(false, true, "/a.epub", recents, gone, true), "");
  EXPECT_EQ(wakebook::bookToOpen(false, true, "/c.epub", recents, onCard, true), "");
  EXPECT_EQ(wakebook::bookToOpen(false, true, "", recents, onCard, true), "");
  EXPECT_EQ(wakebook::bookToOpen(false, false, "/a.epub", recents, onCard, true), "");
}
