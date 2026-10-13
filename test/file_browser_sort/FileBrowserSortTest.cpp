#include <gtest/gtest.h>
#include <FsHelpers.h>
#include "FileBrowserSort.h"

using filebrowser::SortMode;
TEST(FileBrowserSort, NameMatchesExistingOrder) {
  std::vector<std::string> names{"10/", "2/", "a.epub", "1.epub"};
  auto expected = names;
  FsHelpers::sortFileList(expected);
  filebrowser::sortNames(names, nullptr, SortMode::Name);
  EXPECT_EQ(names, expected);
}
TEST(FileBrowserSort, ModifiedNewestFirstWithNaturalNameTies) {
  std::vector<std::string> names{"old.epub", "new.epub", "same10.epub", "same2.epub"};
  uint64_t keys[]{10, 30, 20, 20};
  filebrowser::sortNames(names, keys, SortMode::Modified);
  EXPECT_EQ(names, (std::vector<std::string>{"new.epub", "same2.epub", "same10.epub", "old.epub"}));
}
TEST(FileBrowserSort, SizeLargestFirstWithFoldersInNaturalOrder) {
  std::vector<std::string> names{"small.epub", "folder10/", "large.epub", "folder2/"};
  uint64_t keys[]{2, 90, UINT64_C(5000000000), 1};
  filebrowser::sortNames(names, keys, SortMode::Size);
  EXPECT_EQ(names, (std::vector<std::string>{"folder2/", "folder10/", "large.epub", "small.epub"}));
}
TEST(FileBrowserSort, MissingKeysFallsBackToName) {
  std::vector<std::string> names{"2.txt", "10.txt", "1.txt"};
  filebrowser::sortNames(names, nullptr, SortMode::Modified);
  EXPECT_EQ(names, (std::vector<std::string>{"1.txt", "2.txt", "10.txt"}));
}
TEST(FileBrowserSort, InvalidValueUsesName) {
  EXPECT_EQ(filebrowser::sortModeFromValue(-1), SortMode::Name);
  EXPECT_EQ(filebrowser::sortModeFromValue(256), SortMode::Name);
}
TEST(FileBrowserSort, HandlesEmptySingletonAndManyRows) {
  for (size_t count : {0u, 1u, 2u, 3u, 371u}) {
    std::vector<std::string> names;
    std::vector<uint64_t> keys;
    for (size_t i = 0; i < count; ++i) {
      names.push_back(std::to_string(i) + ".txt");
      keys.push_back((i * 17) % 97);
    }
    filebrowser::sortNames(names, keys.data(), SortMode::Size);
    for (size_t i = 1; i < count; ++i) {
      EXPECT_GE(keys[i-1], keys[i]);
      if (keys[i-1] == keys[i]) EXPECT_FALSE(FsHelpers::naturalLess(names[i], names[i-1]));
    }
  }
}
