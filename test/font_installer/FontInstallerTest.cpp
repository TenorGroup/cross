// Host tests for the FontInstaller name/path contract (S2/N3).
//
// The real src/FontInstaller.cpp is compiled into this target unchanged: the
// 31/87-byte bounds, the ASCII grammar, the root precedence, the truncation
// refusal and the "reject before anything touches storage" ordering asserted
// here are production logic, not a copy.
//
// Only the ESP-only headers (HalStorage/Logging), the registry's root answers
// and the settings method behind the active-family clear are host stubs; see the
// comments in stubs/ and the two *Stubs.cpp files.

#include <CrossPointSettings.h>
#include <FontInstaller.h>
#include <HalStorage.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "FontInstallerTestHooks.h"

using font_installer_test::existingRoot;
using font_installer_test::writeRoot;

namespace {

constexpr const char* kCpfontExt = ".cpfont";

class FontInstallerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Storage.calls.clear();
    Storage.present = false;
    Storage.failRemove = false;
    font_installer_test::resetRegistryStub();
    SETTINGS.sdFontFamilyName[0] = '\0';
  }

  static std::string repeat(char c, size_t n) { return std::string(n, c); }

  static std::string longestFamily() { return repeat('a', FontInstaller::MAX_FAMILY_NAME_LEN); }

  /// The longest accepted filename: maximum basename plus the exact suffix.
  static std::string longestFilename() {
    return repeat('f', FontInstaller::MAX_CPFONT_FILENAME_LEN - std::strlen(kCpfontExt)) + kCpfontExt;
  }

  static void setActiveFamily(const char* name) {
    std::strncpy(SETTINGS.sdFontFamilyName, name, sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  }
};

// ---------------------------------------------------------------- family name

TEST_F(FontInstallerTest, FamilyNameAcceptsTheBoundAndTheGrammar) {
  const std::string maxName = repeat('a', FontInstaller::MAX_FAMILY_NAME_LEN);
  ASSERT_EQ(maxName.size(), 31u);
  EXPECT_TRUE(FontInstaller::isValidFamilyName(maxName.c_str()));
  EXPECT_TRUE(FontInstaller::isValidFamilyName("A"));
  EXPECT_TRUE(FontInstaller::isValidFamilyName("Bokerlam"));
  EXPECT_TRUE(FontInstaller::isValidFamilyName("Family-2_Test"));
  EXPECT_TRUE(FontInstaller::isValidFamilyName("0123456789"));
  // The name ends at the NUL: bytes after it are not part of it.
  EXPECT_TRUE(FontInstaller::isValidFamilyName("nul\0hidden"));
}

TEST_F(FontInstallerTest, FamilyNameRejectsOneOverTheBound) {
  EXPECT_FALSE(FontInstaller::isValidFamilyName(repeat('a', FontInstaller::MAX_FAMILY_NAME_LEN + 1).c_str()));
  // 31 valid bytes plus one more: still over the bound, not silently truncated.
  EXPECT_FALSE(FontInstaller::isValidFamilyName((repeat('a', FontInstaller::MAX_FAMILY_NAME_LEN) + "!").c_str()));
}

TEST_F(FontInstallerTest, FamilyNameRejectsNullEmptySeparatorsAndTraversal) {
  EXPECT_FALSE(FontInstaller::isValidFamilyName(nullptr));
  EXPECT_FALSE(FontInstaller::isValidFamilyName(""));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("."));
  EXPECT_FALSE(FontInstaller::isValidFamilyName(".."));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a/.."));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("../etc"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a/b"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("a\\b"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("/abs"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("hidden.family"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("has space"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("tab\tname"));
}

TEST_F(FontInstallerTest, FamilyNameRejectsNonAsciiBytes) {
  EXPECT_FALSE(FontInstaller::isValidFamilyName("caf\xC3\xA9"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("\xE5\xAD\x97\xE4\xBD\x93"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("\xF0\x9F\x98\x80"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("Fam\x80ily"));
  EXPECT_FALSE(FontInstaller::isValidFamilyName("Fam\xFFily"));
}

// ------------------------------------------------------------------ filename

TEST_F(FontInstallerTest, FilenameAcceptsTheBoundAndTheGrammar) {
  const std::string maxName = longestFilename();
  ASSERT_EQ(maxName.size(), 87u);
  EXPECT_TRUE(FontInstaller::isValidCpfontFilename(maxName.c_str()));
  EXPECT_TRUE(FontInstaller::isValidCpfontFilename("a.cpfont"));
  EXPECT_TRUE(FontInstaller::isValidCpfontFilename("Bold_Italic-2.cpfont"));
  // The on-card v4 naming shape.
  EXPECT_TRUE(FontInstaller::isValidCpfontFilename("Bokerlam_16.cpfont"));
}

TEST_F(FontInstallerTest, FilenameRejectsOneOverTheBound) {
  const std::string overName =
      repeat('f', FontInstaller::MAX_CPFONT_FILENAME_LEN - std::strlen(kCpfontExt) + 1) + kCpfontExt;
  ASSERT_EQ(overName.size(), 88u);
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename(overName.c_str()));
}

TEST_F(FontInstallerTest, FilenameRejectsNullEmptyBadSuffixAndMissingBasename) {
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename(nullptr));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename(""));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename(".cpfont"));      // suffix with no basename
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("font.cpfon"));   // short suffix
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("font.cpfontx")); // trailing byte
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("font.CPFONT"));  // exact suffix, case sensitive
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("font.cpfont.tmp"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("font.txt"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("nul\0font.cpfont"));
}

TEST_F(FontInstallerTest, FilenameRejectsSeparatorsTraversalAndNonAscii) {
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("../font.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("a/../font.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("dir/font.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("dir\\font.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("/etc/font.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("fo nt.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("caf\xC3\xA9.cpfont"));
  EXPECT_FALSE(FontInstaller::isValidCpfontFilename("\xE5\xAD\x97.cpfont"));
}

// ------------------------------------------------------------- buildFontPath

TEST_F(FontInstallerTest, BuildFontPathPrefersTheInstalledRootOverTheWriteRoot) {
  existingRoot = SdCardFontRegistry::FONTS_DIR_VISIBLE;
  writeRoot = SdCardFontRegistry::FONTS_DIR_HIDDEN;
  char path[FontInstaller::MAX_FONT_PATH_SIZE];
  ASSERT_TRUE(FontInstaller::buildFontPath("MyFam", "Book.cpfont", path, sizeof(path)));
  EXPECT_STREQ(path, "/fonts/MyFam/Book.cpfont");

  // Not installed anywhere: the default-write root takes over.
  existingRoot = nullptr;
  ASSERT_TRUE(FontInstaller::buildFontPath("MyFam", "Book.cpfont", path, sizeof(path)));
  EXPECT_STREQ(path, "/.fonts/MyFam/Book.cpfont");
}

TEST_F(FontInstallerTest, BuildFontPathFitsTheLongestNamesExactly) {
  existingRoot = SdCardFontRegistry::FONTS_DIR_HIDDEN;
  const std::string family = longestFamily();
  const std::string filename = longestFilename();

  char path[FontInstaller::MAX_FONT_PATH_SIZE];
  ASSERT_TRUE(FontInstaller::buildFontPath(family.c_str(), filename.c_str(), path, sizeof(path)));
  EXPECT_EQ(std::string(path), "/.fonts/" + family + "/" + filename);
  // 127 bytes + NUL: exactly the 128-byte caller buffers this path must fit.
  EXPECT_EQ(std::strlen(path) + 1, sizeof(path));
}

TEST_F(FontInstallerTest, BuildFontPathRejectsInvalidInputWithoutRegistryOrStorageAccess) {
  existingRoot = SdCardFontRegistry::FONTS_DIR_VISIBLE;
  const std::string overlongFamily = repeat('a', FontInstaller::MAX_FAMILY_NAME_LEN + 1);
  const std::string overlongFilename =
      repeat('f', FontInstaller::MAX_CPFONT_FILENAME_LEN - std::strlen(kCpfontExt) + 1) + kCpfontExt;
  const struct {
    const char* family;
    const char* filename;
  } rejected[] = {
      {nullptr, "Book.cpfont"},         {"MyFam", nullptr},
      {"", "Book.cpfont"},              {"..", "Book.cpfont"},
      {"My/Fam", "Book.cpfont"},        {"MyFam", "Book.txt"},
      {"MyFam", "../../settings.json"}, {overlongFamily.c_str(), "Book.cpfont"},
      {"MyFam", overlongFilename.c_str()},
  };

  char path[FontInstaller::MAX_FONT_PATH_SIZE];
  const int lookupsBefore = font_installer_test::registryRootLookups;
  for (const auto& c : rejected) {
    const char* family = c.family ? c.family : "(null)";
    const char* filename = c.filename ? c.filename : "(null)";
    std::memset(path, 'X', sizeof(path));
    EXPECT_FALSE(FontInstaller::buildFontPath(c.family, c.filename, path, sizeof(path)))
        << "family=" << family << " filename=" << filename;
    EXPECT_EQ(path[0], '\0') << "family=" << family << " filename=" << filename;
  }
  EXPECT_EQ(font_installer_test::registryRootLookups, lookupsBefore);
  EXPECT_TRUE(Storage.calls.empty());
}

TEST_F(FontInstallerTest, BuildFontPathRefusesNullZeroSizeAndTinyBuffers) {
  char path[FontInstaller::MAX_FONT_PATH_SIZE];
  EXPECT_FALSE(FontInstaller::buildFontPath("MyFam", "Book.cpfont", nullptr, 0));
  EXPECT_FALSE(FontInstaller::buildFontPath("MyFam", "Book.cpfont", nullptr, sizeof(path)));

  std::memset(path, 'X', sizeof(path));
  EXPECT_FALSE(FontInstaller::buildFontPath("MyFam", "Book.cpfont", path, 0));
  EXPECT_EQ(path[0], 'X');  // a zero-size buffer is not writable at all

  char oneByte[1] = {'X'};
  EXPECT_FALSE(FontInstaller::buildFontPath("MyFam", "Book.cpfont", oneByte, sizeof(oneByte)));
  EXPECT_EQ(oneByte[0], '\0');

  char tiny[8];
  std::memset(tiny, 'X', sizeof(tiny));
  EXPECT_FALSE(FontInstaller::buildFontPath("MyFam", "Book.cpfont", tiny, sizeof(tiny)));
  EXPECT_EQ(tiny[0], '\0');
}

TEST_F(FontInstallerTest, BuildFontPathNeverReturnsATruncatedPath) {
  existingRoot = SdCardFontRegistry::FONTS_DIR_HIDDEN;
  const std::string family = longestFamily();
  const std::string filename = longestFilename();

  char buffer[FontInstaller::MAX_FONT_PATH_SIZE];
  std::memset(buffer, 'X', sizeof(buffer));
  // The complete path is 127 bytes; one byte short must fail rather than emit a
  // truncated path that a caller would then open.
  EXPECT_FALSE(FontInstaller::buildFontPath(family.c_str(), filename.c_str(), buffer, sizeof(buffer) - 1));
  EXPECT_EQ(buffer[0], '\0');
}

// ----------------------------------------------------------- ensureFamilyDir

TEST_F(FontInstallerTest, EnsureFamilyDirRejectsInvalidFamiliesBeforeRegistryAndStorage) {
  const char* rejected[] = {nullptr, "", "..", "a/b", "dir\\sub", "has space", "caf\xC3\xA9", "hidden.dot"};
  const std::string overlongFamily = repeat('a', FontInstaller::MAX_FAMILY_NAME_LEN + 1);

  SdCardFontRegistry registry;
  FontInstaller installer(registry);
  for (const char* family : rejected) {
    const char* label = family ? family : "(null)";
    Storage.calls.clear();
    font_installer_test::resetRegistryStub();
    EXPECT_FALSE(installer.ensureFamilyDir(family)) << "family=" << label;
    EXPECT_TRUE(Storage.calls.empty()) << "family=" << label;
    EXPECT_EQ(font_installer_test::registryRootLookups, 0) << "family=" << label;
  }

  Storage.calls.clear();
  font_installer_test::resetRegistryStub();
  EXPECT_FALSE(installer.ensureFamilyDir(overlongFamily.c_str()));
  EXPECT_TRUE(Storage.calls.empty());
  EXPECT_EQ(font_installer_test::registryRootLookups, 0);
}


TEST_F(FontInstallerTest, EnsureFamilyDirCreatesTheCompleteLongestPath) {
  existingRoot = SdCardFontRegistry::FONTS_DIR_HIDDEN;
  const std::string family = longestFamily();
  SdCardFontRegistry registry;
  FontInstaller installer(registry);

  ASSERT_TRUE(installer.ensureFamilyDir(family.c_str()));
  ASSERT_EQ(Storage.calls.size(), 4u);
  // 7-byte root + '/' + the full 31-byte family name: never a truncated dir.
  EXPECT_EQ(Storage.calls[3], "mkdir:/.fonts/" + family);
}

// -------------------------------------------------------------- deleteFamily

TEST_F(FontInstallerTest, DeleteFamilyRejectsInvalidFamiliesWithoutStorageEffects) {
  setActiveFamily("MyFam");
  const char* rejected[] = {nullptr, "", "..", "a/b", "dir\\sub", "has space", "caf\xC3\xA9", "hidden.dot"};
  const std::string overlongFamily = repeat('a', FontInstaller::MAX_FAMILY_NAME_LEN + 1);

  SdCardFontRegistry registry;
  FontInstaller installer(registry);
  for (const char* family : rejected) {
    const char* label = family ? family : "(null)";
    Storage.calls.clear();
    EXPECT_EQ(installer.deleteFamily(family), FontInstaller::Error::INVALID_FAMILY_NAME) << "family=" << label;
    EXPECT_TRUE(Storage.calls.empty()) << "family=" << label;
  }

  Storage.calls.clear();
  EXPECT_EQ(installer.deleteFamily(overlongFamily.c_str()), FontInstaller::Error::INVALID_FAMILY_NAME);
  EXPECT_TRUE(Storage.calls.empty());
  // Nothing was deleted, so the active family must still be selected.
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "MyFam");
}

TEST_F(FontInstallerTest, DeleteFamilyRemovesTheCompletePathFromBothRoots) {
  Storage.present = true;
  setActiveFamily("MyFam");
  SdCardFontRegistry registry;
  FontInstaller installer(registry);

  ASSERT_EQ(installer.deleteFamily("MyFam"), FontInstaller::Error::OK);
  const std::vector<std::string> expected = {"exists:/.fonts/MyFam", "remove:/.fonts/MyFam",
                                             "exists:/fonts/MyFam",  "remove:/fonts/MyFam"};
  EXPECT_EQ(Storage.calls, expected);
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "");  // the active family is gone
}

TEST_F(FontInstallerTest, DeleteFamilyKeepsAnotherActiveFamily) {
  Storage.present = true;
  setActiveFamily("Other");
  SdCardFontRegistry registry;
  FontInstaller installer(registry);

  ASSERT_EQ(installer.deleteFamily("MyFam"), FontInstaller::Error::OK);
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "Other");
}

TEST_F(FontInstallerTest, DeleteFamilyUsesTheCompleteLongestPath) {
  Storage.present = true;
  const std::string family = longestFamily();
  SdCardFontRegistry registry;
  FontInstaller installer(registry);

  ASSERT_EQ(installer.deleteFamily(family.c_str()), FontInstaller::Error::OK);
  ASSERT_EQ(Storage.calls.size(), 4u);
  EXPECT_EQ(Storage.calls[1], "remove:/.fonts/" + family);
}


TEST_F(FontInstallerTest, DeleteFamilyReportsStorageFailureAndKeepsTheActiveFont) {
  Storage.present = true;
  Storage.failRemove = true;
  setActiveFamily("MyFam");
  SdCardFontRegistry registry;
  FontInstaller installer(registry);

  ASSERT_EQ(installer.deleteFamily("MyFam"), FontInstaller::Error::SD_WRITE_ERROR);
  // A half-finished delete must not leave the reader pointed at nothing.
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "MyFam");
}

}  // namespace
