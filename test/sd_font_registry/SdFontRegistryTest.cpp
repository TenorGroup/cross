#include <HalStorage.h>
#include <SdCardFontRegistry.h>
#include <gtest/gtest.h>

#include <string>

namespace {

std::string pathOf(const SdCardFontFamilyInfo& family, const SdCardFontFileInfo& file) {
  char out[SdCardFontFamilyInfo::PATH_BUFFER_SIZE];
  EXPECT_TRUE(family.filePath(file, out, sizeof(out)));
  return out;
}

class SdFontRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override { fake::reset(); }
  void TearDown() override { fake::reset(); }
  SdCardFontRegistry registry;
};

// The registry keeps the stem once per family and the size per file, not a path per file (18/09/2026).
TEST_F(SdFontRegistryTest, CanonicalNamesStoreNoFileString) {
  fake::add("/.fonts/Bitter/Bitter_12.cpfont");
  fake::add("/.fonts/Bitter/Bitter_14.cpfont");

  ASSERT_TRUE(registry.discover());
  const auto* family = registry.findFamily("Bitter");
  ASSERT_NE(family, nullptr);
  EXPECT_TRUE(family->hiddenRoot);
  ASSERT_EQ(family->files.size(), 2u);
  ASSERT_EQ(family->stems.size(), 1u);
  EXPECT_EQ(family->stems[0], "Bitter");
  for (const auto& file : family->files) EXPECT_EQ(file.stem, 0u);

  const auto* file14 = family->findFile(14);
  ASSERT_NE(file14, nullptr);
  EXPECT_EQ(pathOf(*family, *file14), "/.fonts/Bitter/Bitter_14.cpfont");
  EXPECT_EQ(pathOf(*family, *family->findFile(12)), "/.fonts/Bitter/Bitter_12.cpfont");
}

TEST_F(SdFontRegistryTest, OtherStemsKeepTheirOwnStemEntry) {
  fake::add("/fonts/Bookerly/Bookerly-SD_14.cpfont");
  fake::add("/fonts/Bookerly/Bookerly_16.cpfont");

  ASSERT_TRUE(registry.discover());
  const auto* family = registry.findFamily("Bookerly");
  ASSERT_NE(family, nullptr);
  EXPECT_FALSE(family->hiddenRoot);
  ASSERT_EQ(family->stems.size(), 2u);

  const auto* file14 = family->findFile(14);
  ASSERT_NE(file14, nullptr);
  EXPECT_EQ(family->stems[file14->stem], "Bookerly-SD");
  EXPECT_EQ(pathOf(*family, *file14), "/fonts/Bookerly/Bookerly-SD_14.cpfont");

  const auto* file16 = family->findFile(16);
  ASSERT_NE(file16, nullptr);
  EXPECT_EQ(family->stems[file16->stem], "Bookerly");
  EXPECT_EQ(pathOf(*family, *file16), "/fonts/Bookerly/Bookerly_16.cpfont");
}

TEST_F(SdFontRegistryTest, HiddenRootWinsAndKeepsItsOwnRoot) {
  fake::add("/.fonts/Lora/Lora_14.cpfont");
  fake::add("/fonts/Lora/Lora_18.cpfont");
  fake::add("/fonts/Inter/Inter_14.cpfont");

  ASSERT_TRUE(registry.discover());
  ASSERT_EQ(registry.getFamilyCount(), 2);
  const auto* lora = registry.findFamily("Lora");
  ASSERT_NE(lora, nullptr);
  EXPECT_TRUE(lora->hiddenRoot);
  EXPECT_FALSE(lora->hasSize(18));
  const auto* inter = registry.findFamily("Inter");
  ASSERT_NE(inter, nullptr);
  EXPECT_FALSE(inter->hiddenRoot);
  EXPECT_EQ(pathOf(*inter, inter->files[0]), "/fonts/Inter/Inter_14.cpfont");
}

TEST_F(SdFontRegistryTest, OverlongPathsAreSkippedAtDiscovery) {
  // A family folder name holds at most MAX_FAMILY_NAME_BYTES (63: the settings keep it in a 64-byte field), so a
  // long path comes from a long file name: "/.fonts/" + 63 + "/" + 100 + "_14.cpfont" = 182 bytes, over PATH_BUFFER_SIZE.
  const std::string longName(63, 'L');
  fake::add("/.fonts/" + longName + "/" + std::string(100, 'L') + "_14.cpfont");
  fake::add("/.fonts/" + longName + "/Short_16.cpfont");  // 87 bytes: fits
  // The 110-byte filename fits the 128-byte name buffer, but the 182-byte path does not fit PATH_BUFFER_SIZE.
  const std::string onlyLong(63, 'O');
  fake::add("/.fonts/" + onlyLong + "/" + std::string(100, 'O') + "_12.cpfont");
  // A folder name over the cap never becomes a family, whatever it holds.
  const std::string tooLongName(SdCardFontRegistry::MAX_FAMILY_NAME_BYTES + 1, 'N');
  fake::add("/.fonts/" + tooLongName + "/Short_14.cpfont");

  ASSERT_TRUE(registry.discover());
  EXPECT_EQ(registry.findFamily(onlyLong), nullptr);  // no loadable file left
  EXPECT_EQ(registry.findFamily(tooLongName), nullptr);
  const auto* family = registry.findFamily(longName);
  ASSERT_NE(family, nullptr);
  ASSERT_EQ(family->files.size(), 1u);
  EXPECT_EQ(family->findFile(14), nullptr);
  const auto* nearest = family->findNearestSize(14);
  ASSERT_NE(nearest, nullptr);
  EXPECT_EQ(nearest->pointSize, 16);
  EXPECT_EQ(pathOf(*family, *nearest), "/.fonts/" + longName + "/Short_16.cpfont");
}

TEST_F(SdFontRegistryTest, FilePathReportsTruncation) {
  fake::add("/.fonts/Bitter/Bitter_14.cpfont");
  ASSERT_TRUE(registry.discover());
  const auto* family = registry.findFamily("Bitter");
  ASSERT_NE(family, nullptr);

  char tiny[16];
  EXPECT_FALSE(family->filePath(family->files[0], tiny, sizeof(tiny)));
  EXPECT_EQ(tiny[sizeof(tiny) - 1], '\0');
}

// parseFilename takes a zero padded size, so the path the registry builds must give the file's own name back
// (Bookerly_016.cpfont, not Bookerly_16.cpfont, which is not on the card).
TEST_F(SdFontRegistryTest, ZeroPaddedSizesOpenTheFileTheyCameFrom) {
  fake::add("/fonts/Bookerly/Bookerly_016.cpfont");
  fake::add("/fonts/Bookerly/Bookerly_18.cpfont");
  fake::add("/fonts/Bookerly/weight-2/Bookerly_016.cpfont");

  ASSERT_TRUE(registry.discover());
  const auto* family = registry.findFamily("Bookerly");
  ASSERT_NE(family, nullptr);
  const auto* padded = family->findFile(16);
  ASSERT_NE(padded, nullptr);
  EXPECT_EQ(pathOf(*family, *padded), "/fonts/Bookerly/Bookerly_016.cpfont");
  EXPECT_EQ(family->filePath(*padded, 2), "/fonts/Bookerly/weight-2/Bookerly_016.cpfont");
  EXPECT_TRUE(Storage.exists(pathOf(*family, *padded).c_str()));
  EXPECT_TRUE(family->hasWeight(*padded, 2));
  EXPECT_EQ(pathOf(*family, *family->findFile(18)), "/fonts/Bookerly/Bookerly_18.cpfont");
}

}  // namespace
