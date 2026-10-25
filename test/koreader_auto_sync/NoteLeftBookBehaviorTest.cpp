// What the reader notes when it is left: a book owed a push to the sync server is an Epub, never a text file
// (its position is a place in a generated chapter, which no other KOReader can open), as the wake pull already
// holds (pullOnWake). The production noteLeftBook is extracted from src/KOReaderAutoSync.cpp.
#include <gtest/gtest.h>

#include <cmath>
#include <ctime>
#include <string>

#include "CrossPointPosition.h"
#include "KOReaderAutoSyncPolicy.h"
#include "KOReaderSyncDebt.h"

namespace {
struct FakeActivityManager {
  int deferred = 0;
  void deferWrite(void (*)()) { ++deferred; }
} activityManager;
}  // namespace

namespace koautosync {
namespace {
using namespace kosyncauto;
SyncDebt pendingNote;
bool hasPendingNote = false;
int flushed = 0;
void flushNote() { ++flushed; }
}  // namespace

#include "KOReaderNoteLeftBook.inc"
}  // namespace koautosync

namespace {
using namespace koautosync;

class NoteLeftBook : public testing::Test {
 protected:
  void SetUp() override {
    hasPendingNote = false;
    pendingNote = {};
    flushed = 0;
    activityManager.deferred = 0;
  }
  static CrossPointPosition place() {
    CrossPointPosition p{};
    p.spineIndex = 0;
    p.pageNumber = 3;
    p.totalPages = 10;
    return p;
  }
};

TEST_F(NoteLeftBook, AnEpubIsOwedAPushAtSleepOrAfterTheNextFrame) {
  noteLeftBook("/Books/a.epub", place(), 0.3f, false);
  EXPECT_TRUE(hasPendingNote);
  EXPECT_EQ(activityManager.deferred, 1);
  EXPECT_EQ(flushed, 0);
  SetUp();
  noteLeftBook("/Books/a.EPUB", place(), 0.3f, true);
  EXPECT_TRUE(hasPendingNote);
  EXPECT_EQ(flushed, 1);
}

TEST_F(NoteLeftBook, ATextOrXtcBookIsOwedNothing) {
  for (const char* path : {"/b.txt", "/Books/notes.md", "/Books/comic.xtc", "/Books/a.epub.txt", "/epub"}) {
    for (const bool sleeping : {false, true}) {
      SetUp();
      noteLeftBook(path, place(), 0.3f, sleeping);
      EXPECT_FALSE(hasPendingNote) << path;
      EXPECT_EQ(activityManager.deferred, 0) << path;
      EXPECT_EQ(flushed, 0) << path;
    }
  }
}
}  // namespace
