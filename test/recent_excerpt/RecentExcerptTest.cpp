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

// Reopening a listed book moves its entry to the front instead of copying it: the reader adds the
// book after its first render, and fresh string buffers would land among the reader's allocations
// and outlive them, splitting the largest free block once the reader exits (CrossPoint #3830).
TEST(RecentExcerpt, ReopenedBookKeepsItsStringsAndExcerpt) {
  auto& store = fresh();
  const std::string title = "A title long enough to live on the heap";
  store.addBook("/books/b.epub", title, "An author name past the inline buffer", "/covers/b-cover-thumbnail.bmp");
  ASSERT_TRUE(store.rememberExcerpt("/books/b.epub", "Lines from book b."));
  store.addBook("/books/c.epub", "C", "", "");
  ASSERT_EQ(store.getBooks()[1].path, "/books/b.epub");
  const char* titleBuffer = store.getBooks()[1].title.data();
  const char* authorBuffer = store.getBooks()[1].author.data();

  store.addBook("/books/b.epub", title, "An author name past the inline buffer", "/covers/b-cover-thumbnail.bmp");

  const auto& books = store.getBooks();
  ASSERT_EQ(books[0].path, "/books/b.epub");
  EXPECT_EQ(books[1].path, "/books/c.epub");
  EXPECT_EQ(books[2].path, "/books/a.epub");
  EXPECT_EQ(books[0].title.data(), titleBuffer) << "the reopened entry's title was copied";
  EXPECT_EQ(books[0].author.data(), authorBuffer) << "the reopened entry's author was copied";
  EXPECT_EQ(books[0].excerpt, "Lines from book b.");

  store.addBook("/books/b.epub", "A new title", "An author name past the inline buffer", "/covers/b-cover-thumbnail.bmp");
  EXPECT_EQ(store.getBooks()[0].title, "A new title");
  EXPECT_EQ(store.getBooks().size(), 3u);
}
