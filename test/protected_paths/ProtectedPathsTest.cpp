#include <gtest/gtest.h>

#include "network/ProtectedPaths.h"

using protectedpaths::isPluginPath;
using protectedpaths::isSensitivePath;

TEST(ProtectedPaths, CredentialStoresAreSensitive) {
  EXPECT_TRUE(isSensitivePath("/.crosspoint/wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/opds.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/koreader.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/wifi.json.tmp"));
  EXPECT_TRUE(isSensitivePath("/.CrossPoint/WIFI.json"));
}

TEST(ProtectedPaths, SpellingVariantsCannotDodgeTheMatch) {
  EXPECT_TRUE(isSensitivePath("/.crosspoint/./wifi.json"));
  EXPECT_TRUE(isSensitivePath("//.crosspoint//wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/plugins/../wifi.json"));
  EXPECT_TRUE(isSensitivePath(".crosspoint/opds.json"));
}

TEST(ProtectedPaths, NamesSdFatWouldTrimOrAliasStillMatch) {
  EXPECT_TRUE(isSensitivePath("/.crosspoint/wifi.json."));
  EXPECT_TRUE(isSensitivePath("/.crosspoint./wifi.json"));
  EXPECT_TRUE(isSensitivePath("/ .crosspoint/ wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint ./opds.json"));
  EXPECT_TRUE(isSensitivePath("/CROSSP~1/wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/WIFI~1.JSO"));
  EXPECT_FALSE(isSensitivePath("/Books/Photos/IMG~1.JPG"));  // too deep to reach a store
  EXPECT_FALSE(isSensitivePath("/Books/a~b.epub"));
}

TEST(ProtectedPaths, EverythingElseStaysReachable) {
  EXPECT_FALSE(isSensitivePath("/.crosspoint/settings.json"));
  EXPECT_FALSE(isSensitivePath("/.crosspoint/library.idx"));
  EXPECT_FALSE(isSensitivePath("/.crosspoint/month-wallpaper.json"));
  EXPECT_FALSE(isSensitivePath("/.fonts/Noto/Noto_14.cpfont"));
  EXPECT_FALSE(isSensitivePath("/Books/wifi.json"));
}

TEST(ProtectedPaths, PluginPathRules) {
  EXPECT_FALSE(isPluginPath("/.crosspoint/plugins/../wifi.json"));
  EXPECT_FALSE(isPluginPath("/.crosspoint/./koreader.json"));
  EXPECT_FALSE(isPluginPath("relative/path"));
  EXPECT_FALSE(isPluginPath("/"));
  EXPECT_TRUE(isPluginPath("/Books/new.epub"));
}

// A plugin writes (fetch, plugin-fs) and names token files; /.crosspoint holds the settings, the WebDAV login, the
// OPDS tokens and every other store of the device, so a plugin gets its own folder there and no more, as the file
// manager's WebPathPolicy keeps the whole tree out of reach.
TEST(ProtectedPaths, APluginKeepsToItsOwnFolderInsideCrosspoint) {
  for (const char* path : {"/.crosspoint/settings.json", "/.crosspoint/webdav.json", "/.crosspoint/opds_tokens.json",
                           "/.crosspoint/library.idx", "/.crosspoint/month-wallpaper.json", "/.crosspoint/plugins",
                           "/.crosspoint/plugins/", "/.crosspoint/plugins/foo", "/.crosspoint/plugins.json",
                           "/.crosspoint/other/foo/x.json", "/.CrossPoint/state.json", "//.crosspoint//settings.json",
                           "/.crosspoint/./settings.json", "/ .crosspoint/settings.json",
                           "/.crosspoint ./settings.json", "/.crosspoint./settings.json",
                           "/.crosspoint/plugins./settings.json", "/CROSSP~1/settings.json"}) {
    EXPECT_FALSE(isPluginPath(path)) << path;
  }
  for (const char* path : {"/.crosspoint/plugins/foo/token.json", "/.crosspoint/plugins/foo/a/b.json",
                           "/.CrossPoint/Plugins/foo/x.json", "//.crosspoint//plugins//foo//x.json",
                           "/plugins/foo/config.json", "/.plugins/foo/config.json", "/Books/a.epub", "/.hidden/file",
                           "/Books/.crosspoint/x.json", "/crosspoint/settings.json"}) {
    EXPECT_TRUE(isPluginPath(path)) << path;
  }
}
