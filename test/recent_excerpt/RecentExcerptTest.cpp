// v1.0.14: the reader leaves recent.json for after Home's first frame. A card that refuses that
// write must not lose the excerpt for good: it stays owed in the store, so the next exit from any
// book writes it again, and only a successful write settles it.
#include <gtest/gtest.h>

#include "RecentBooksStore.h"

namespace {
RecentBooksStore& fresh() {
  auto& store = RECENT_BOOKS;
  recentStoreFake::saveWorks = true;
  store.addBook("/books/a.epub", "A", "", "");
  while (store.hasUnsavedExcerpt()) store.saveExcerpt();
  recentStoreFake::saves = 0;
  return store;
}
}  // namespace

TEST(RecentExcerpt, ChangedExcerptIsOwedUntilAWriteSucceeds) {
  auto& store = fresh();
  ASSERT_TRUE(store.rememberExcerpt("/books/a.epub", "First lines of the page."));
  EXPECT_TRUE(store.hasUnsavedExcerpt());
  recentStoreFake::saveWorks = false;
  EXPECT_FALSE(store.saveExcerpt());
  EXPECT_TRUE(store.hasUnsavedExcerpt()) << "a refused write dropped the owed excerpt";
  // The next visit leaves the excerpt as it is; it is still owed and written this time.
  EXPECT_FALSE(store.rememberExcerpt("/books/a.epub", "First lines of the page."));
  EXPECT_TRUE(store.hasUnsavedExcerpt());
  recentStoreFake::saveWorks = true;
  EXPECT_TRUE(store.saveExcerpt());
  EXPECT_FALSE(store.hasUnsavedExcerpt());
  EXPECT_EQ(recentStoreFake::saves, 2);
}

TEST(RecentExcerpt, UnchangedExcerptOwesNothing) {
  auto& store = fresh();
  ASSERT_TRUE(store.rememberExcerpt("/books/a.epub", "Same text."));
  ASSERT_TRUE(store.saveExcerpt());
  EXPECT_FALSE(store.rememberExcerpt("/books/a.epub", "Same text."));
  EXPECT_FALSE(store.hasUnsavedExcerpt());
}
