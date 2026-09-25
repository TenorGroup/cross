// Recent entries saved before the card-shaped thumbnails name the old stretched EPUB files.
// Only EPUB caches moved to the new name; other book types still write the old one.
#include <gtest/gtest.h>

#include "util/ThumbName.h"

namespace {
std::string moved(std::string path) {
  thumbname::moveOldEpubThumb(path);
  return path;
}
}  // namespace

TEST(ThumbName, OldEpubThumbnailMovesToTheNewName) {
  EXPECT_EQ(moved("/.crosspoint/epub_123/thumb_[HEIGHT].bmp"), "/.crosspoint/epub_123/thumb2_[HEIGHT].bmp");
}

TEST(ThumbName, OtherBookTypesKeepTheirThumbnail) {
  EXPECT_EQ(moved("/.crosspoint/xtc_123/thumb_[HEIGHT].bmp"), "/.crosspoint/xtc_123/thumb_[HEIGHT].bmp");
}

TEST(ThumbName, NewOrEmptyPathsStayAsTheyAre) {
  EXPECT_EQ(moved("/.crosspoint/epub_123/thumb2_[HEIGHT].bmp"), "/.crosspoint/epub_123/thumb2_[HEIGHT].bmp");
  EXPECT_EQ(moved(""), "");
  EXPECT_EQ(moved("/.crosspoint/epub_123/cover.bmp"), "/.crosspoint/epub_123/cover.bmp");
}
