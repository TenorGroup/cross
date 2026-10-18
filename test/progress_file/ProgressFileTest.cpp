// A save of the reading place that the card cuts short leaves the old place or the new one, never none.
#include <gtest/gtest.h>

#include "src/activities/reader/ProgressFile.h"

namespace {
const std::string kDir = "/.crosspoint/epub_1";

bool save(uint8_t page) {
  const uint8_t data[4] = {1, 0, page, 0};
  return ProgressFile::writeAtomic(kDir, data, sizeof(data));
}

// The page the next boot reads, or -1 when it finds no place at all.
int pageAfterRestart() {
  card.changesLeft = 1 << 30;  // the restarted card answers again
  HalFile f;
  uint8_t data[4];
  if (!ProgressFile::openForRead("T", kDir, f) || f.read(data, 4) != 4) return -1;
  return data[2];
}
}  // namespace

TEST(ProgressFile, SavesAndReadsBack) {
  card = {};
  ASSERT_TRUE(save(7));
  EXPECT_EQ(pageAfterRestart(), 7);
  ASSERT_TRUE(save(8));
  EXPECT_EQ(pageAfterRestart(), 8);
  EXPECT_EQ(card.files.size(), 1u);  // only progress.bin stays
}

TEST(ProgressFile, CardDyingAtAnyStepKeepsAPlace) {
  for (int changes = 0; changes < 8; ++changes) {
    card = {};
    ASSERT_TRUE(save(7));
    card.changesLeft = changes;
    const bool saved = save(8);
    const int page = pageAfterRestart();
    EXPECT_TRUE(page == 7 || page == 8) << "card died after " << changes << " changes, read " << page;
    if (saved) EXPECT_EQ(page, 8) << changes;
    // The next save after the restart lands, whatever the dead card left behind.
    EXPECT_TRUE(save(9)) << changes;
    EXPECT_EQ(pageAfterRestart(), 9) << changes;
  }
}

// The card dies again in the first save after the restart: the place the first crash kept must survive.
TEST(ProgressFile, SecondDeathRightAfterTheFirstKeepsAPlace) {
  for (int first = 0; first < 8; ++first) {
    for (int second = 0; second < 8; ++second) {
      card = {};
      ASSERT_TRUE(save(7));
      card.changesLeft = first;
      save(8);
      card.changesLeft = second;
      save(9);
      const int page = pageAfterRestart();
      EXPECT_TRUE(page >= 7 && page <= 9) << "deaths after " << first << " then " << second << ", read " << page;
    }
  }
}
