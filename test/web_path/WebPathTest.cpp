#include <gtest/gtest.h>

#include "src/network/WebPathPolicy.h"

TEST(WebPath, AllowsBooksAndRoot) {
  for (const auto path : {"/", "/Books", "/Books/Book.epub", "/Books/volume..2.epub", "/Books/book~draft.epub",
                          "/Đọc sách/Chương 1.txt"}) {
    EXPECT_TRUE(web_path::allowed(path)) << path;
  }
}

TEST(WebPath, ProtectsEveryAncestor) {
  for (const auto path : {"/.crosspoint/wifi.json", "/Books/.hidden/book.epub", "/XTCache/file", "/xtcache/file",
                          "/SYSTEM VOLUME INFORMATION/file", "/.crosspoint", "/Books/../.crosspoint/wifi.json"}) {
    EXPECT_FALSE(web_path::allowed(path)) << path;
  }
}

TEST(WebPath, RejectsAmbiguousFatNamesAndControlBytes) {
  for (const auto path : {"", "../book", "/Books/./book", "/Books/../book", "/XTCache /file", "/XTCache./file",
                          "/CROSSP~1/WIFI.JSON", "/Books\\.crosspoint\\wifi.json", "/Books/a\r\n.txt", "/Books/a\".txt",
                          "/Books/file:ads", "/Books/file?.txt"}) {
    EXPECT_FALSE(web_path::allowed(path)) << path;
  }
  EXPECT_FALSE(web_path::allowed(std::string_view("/Books/\0x", 9)));
}
