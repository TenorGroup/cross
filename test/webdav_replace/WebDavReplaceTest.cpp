#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>

#include "src/network/WebDavReplace.h"

struct Store {
  std::map<std::string, std::string> files{{"temp", "new"}, {"book", "original"}};
  std::set<std::string> failedRenames;
  bool failRemove = false;
  bool exists(const char* p) { return files.count(p); }
  bool rename(const char* a, const char* b) {
    if (failedRenames.count(a) || !files.count(a) || files.count(b)) return false;
    files[b] = files.at(a);
    files.erase(a);
    return true;
  }
  bool remove(const char* p) { return !failRemove && files.erase(p); }
};
TEST(WebDavReplace, CommitsAndRemovesBackup) {
  Store s;
  ASSERT_TRUE(webdav::replaceFile(s, "temp", "book"));
  EXPECT_EQ(s.files.at("book"), "new");
  EXPECT_EQ(s.files.size(), 1);
}
TEST(WebDavReplace, RestoresOriginalWhenCommitFails) {
  Store s;
  s.failedRenames.insert("temp");
  EXPECT_FALSE(webdav::replaceFile(s, "temp", "book"));
  EXPECT_EQ(s.files.at("book"), "original");
  EXPECT_EQ(s.files.at("temp"), "new");
}
TEST(WebDavReplace, RetainsBackupWhenRestoreAlsoFails) {
  Store s;
  s.failedRenames = {"temp", "book.davbak"};
  EXPECT_FALSE(webdav::replaceFile(s, "temp", "book"));
  EXPECT_EQ(s.files.at("book.davbak"), "original");
  EXPECT_EQ(s.files.at("temp"), "new");
}
TEST(WebDavReplace, BackupFailureLeavesOriginalUntouched) {
  Store s;
  s.failedRenames.insert("book");
  EXPECT_FALSE(webdav::replaceFile(s, "temp", "book"));
  EXPECT_EQ(s.files.at("book"), "original");
}
TEST(WebDavReplace, ExistingBackupIsNeverOverwritten) {
  Store s;
  s.files["book.davbak"] = "recoverable";
  EXPECT_FALSE(webdav::replaceFile(s, "temp", "book"));
  EXPECT_EQ(s.files.at("book"), "original");
  EXPECT_EQ(s.files.at("book.davbak"), "recoverable");
}
TEST(WebDavReplace, CleanupFailurePreservesBothVersions) {
  Store s;
  s.failRemove = true;
  EXPECT_TRUE(webdav::replaceFile(s, "temp", "book"));
  EXPECT_EQ(s.files.at("book"), "new");
  EXPECT_EQ(s.files.at("book.davbak"), "original");
}
TEST(WebDavReplace, CreatesNewDestination) {
  Store s;
  s.files.erase("book");
  EXPECT_TRUE(webdav::replaceFile(s, "temp", "book"));
  EXPECT_EQ(s.files.at("book"), "new");
}

TEST(VerifiedFileInstall, InterruptedDownloadPreservesOldFile) {
  Store s;
  bool validated = false;
  const auto result = webdav::installVerifiedFile(
      s, "book",
      [&](const char* p) {
        s.files[p] = "partial";
        return false;
      },
      [&](const char*) {
        validated = true;
        return true;
      });
  EXPECT_EQ(result, webdav::InstallResult::DOWNLOAD_FAILED);
  EXPECT_FALSE(validated);
  EXPECT_EQ(s.files.at("book"), "original");
  EXPECT_FALSE(s.files.count("book.davtmp"));
}
TEST(VerifiedFileInstall, InvalidChecksumPreservesOldFile) {
  Store s;
  const auto result = webdav::installVerifiedFile(
      s, "book",
      [&](const char* p) {
        s.files[p] = "corrupt";
        return true;
      },
      [&](const char*) { return false; });
  EXPECT_EQ(result, webdav::InstallResult::VALIDATION_FAILED);
  EXPECT_EQ(s.files.at("book"), "original");
  EXPECT_FALSE(s.files.count("book.davtmp"));
}
TEST(VerifiedFileInstall, ValidationPrecedesReplacement) {
  Store s;
  const auto result = webdav::installVerifiedFile(
      s, "book",
      [&](const char* p) {
        s.files[p] = "valid";
        return true;
      },
      [&](const char* p) {
        EXPECT_EQ(s.files.at("book"), "original");
        return s.files.at(p) == "valid";
      });
  EXPECT_EQ(result, webdav::InstallResult::OK);
  EXPECT_EQ(s.files.at("book"), "valid");
  EXPECT_FALSE(s.files.count("book.davtmp"));
  EXPECT_FALSE(s.files.count("book.davbak"));
}
TEST(VerifiedFileInstall, FailedCommitRestoresOldFile) {
  Store s;
  s.failedRenames.insert("book.davtmp");
  const auto result = webdav::installVerifiedFile(
      s, "book",
      [&](const char* p) {
        s.files[p] = "valid";
        return true;
      },
      [&](const char*) { return true; });
  EXPECT_EQ(result, webdav::InstallResult::REPLACE_FAILED);
  EXPECT_EQ(s.files.at("book"), "original");
  EXPECT_FALSE(s.files.count("book.davtmp"));
}
