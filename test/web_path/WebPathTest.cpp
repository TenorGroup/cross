#include <gtest/gtest.h>

#include "src/network/WebPathPolicy.h"

TEST(WebPath, AllowsBooksAndRoot) {
  for (const auto path : {"/", "/Books", "/Books/Book.epub", "/Books/volume..2.epub", "/Books/book~draft.epub",
                          "/Đọc sách/Chương 1.txt", "/Books/.hidden/book.epub", "/.hidden", "/Books/.DS_Store"}) {
    EXPECT_TRUE(web_path::allowed(path)) << path;
  }
}

TEST(WebPath, ProtectsEveryAncestor) {
  for (const auto path : {"/.crosspoint/wifi.json", "/.CrossPoint/opds_tokens.json", "//.crosspoint//webdav.json", "/XTCache/file", "/xtcache/file",
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

// SdFat skips the leading spaces of every name it opens, so " .crosspoint" is the credential tree.
TEST(WebPath, LeadingSpacesDoNotHideAProtectedName) {
  for (const auto path : {"/ .crosspoint/wifi.json", "/  .crosspoint/settings.json", " .crosspoint/opds_tokens.json",
                          "/Books/../ .crosspoint/webdav.json", "/ XTCache/file", "/ System Volume Information/file",
                          "/ CROSSP~1/WIFI.JSON", "/ .crosspoint", "/Books/  /x"}) {
    EXPECT_FALSE(web_path::allowed(path)) << path;
  }
  EXPECT_TRUE(web_path::allowed("/Books/ Leading space.epub"));
}

TEST(WebPath, WebDavRefusesDotNamesAndCredentialStoresWhateverTheSpelling) {
  for (const auto path : {"/.crosspoint/wifi.json", "/ .crosspoint/wifi.json", "/%20.crosspoint/wifi.json",
                          "/ .crosspoint/koreader.json", "/Books/.DS_Store", "/Books/ .DS_Store", "/ .hidden",
                          "/Books/ ._book.epub", "/CROSSP~1/WIFI.JSON", "/ CROSSP~1/WIFI.JSON"}) {
    EXPECT_FALSE(web_path::davAllowed(path)) << path;
  }
  for (const auto path : {"/", "/Books", "/Books/Book.epub", "/Books/ Book.epub", "/Đọc sách/Chương 1.txt"}) {
    EXPECT_TRUE(web_path::davAllowed(path)) << path;
  }
}
