#include <gtest/gtest.h>

#include <SdCardFontManager.h>
#include <esp_system.h>

#include <string>

#include "CrossPointSettings.h"
#include "HostCard.h"
#include "SdCardFontSystem.h"
#include "SdFontBootMemo.h"
#include "fontIds.h"

namespace {

SdCardFontFamilyInfo family(const std::string& name, std::initializer_list<uint8_t> sizes, bool hidden = true) {
  SdCardFontFamilyInfo info;
  info.name = name;
  info.stems = {name + "-SD"};
  info.hiddenRoot = hidden;
  for (const uint8_t size : sizes) info.files.push_back({size, 0, 0});
  return info;
}

void putOnCard(const SdCardFontFamilyInfo& info) {
  hostCatalog.push_back(info);
  for (const auto& file : info.files) hostCardFiles.insert(info.filePath(file));
}

class SdFontBoot : public ::testing::Test {
 protected:
  void SetUp() override {
    hostCatalog.clear();
    hostCardFiles.clear();
    hostLoads.clear();
    hostDiscoveries = 0;
    hostCjk = false;
    hostSettings = CrossPointSettings{};
    putOnCard(family("Bokerlam", {8, 10, 12, 14, 16, 18}));
    putOnCard(family("Other", {14}, false));
    std::strcpy(hostSettings.sdFontFamilyName, "Bokerlam");
  }

  // One boot: a fresh font system, as after a reset, with the RTC memo the last boot left.
  void boot(esp_reset_reason_t reason, SdCardFontSystem& system, GfxRenderer& renderer) {
    hostResetReason = reason;
    hostDiscoveries = 0;
    hostLoads.clear();
    system.begin(renderer);
  }
};

}  // namespace

TEST_F(SdFontBoot, ColdBootWalksTheCardOnceAndLoadsTheSavedFamily) {
  SdCardFontSystem system;
  GfxRenderer renderer;
  boot(ESP_RST_POWERON, system, renderer);
  EXPECT_EQ(hostDiscoveries, 1);
  EXPECT_EQ(hostLoads, std::vector<std::string>{"/.fonts/Bokerlam/Bokerlam-SD_14.cpfont"});
  EXPECT_EQ(system.registry().getFamilyCount(), 2);
  EXPECT_EQ(hostDiscoveries, 1);
}

TEST_F(SdFontBoot, WithoutAnSdFamilyTheCatalogWaitsForItsFirstUse) {
  hostSettings.sdFontFamilyName[0] = '\0';
  SdCardFontSystem system;
  GfxRenderer renderer;
  boot(ESP_RST_POWERON, system, renderer);
  EXPECT_EQ(hostDiscoveries, 0);
  EXPECT_TRUE(hostLoads.empty());
  // The settings list, the web page or the reader menu asks for it: one walk, then kept.
  EXPECT_EQ(system.registry().getFamilyCount(), 2);
  const SdCardFontSystem& shown = system;
  EXPECT_EQ(shown.registry().getFamilyCount(), 2);
  EXPECT_EQ(hostDiscoveries, 1);
}

TEST_F(SdFontBoot, WakeLoadsTheLastFamilyWithoutWalkingTheCard) {
  GfxRenderer coldRenderer;
  SdCardFontSystem cold;
  boot(ESP_RST_POWERON, cold, coldRenderer);
  const auto coldLoads = hostLoads;

  SdCardFontSystem wake;
  GfxRenderer renderer;
  boot(ESP_RST_DEEPSLEEP, wake, renderer);
  EXPECT_EQ(hostDiscoveries, 0);
  EXPECT_EQ(hostLoads, coldLoads);
  EXPECT_EQ(std::string(hostSettings.sdFontFamilyName), "Bokerlam");
  // Opening the book it slept in needs nothing more.
  wake.ensureLoaded(renderer);
  EXPECT_EQ(hostDiscoveries, 0);
  EXPECT_EQ(hostLoads, coldLoads);
  EXPECT_EQ(wake.resolveFontId("Bokerlam", 14), 42);
  // The first screen that lists fonts reads the whole catalog, once.
  EXPECT_EQ(wake.registry().getFamilyCount(), 2);
  EXPECT_EQ(hostDiscoveries, 1);
}

TEST_F(SdFontBoot, OnlyADeepSleepWakeTrustsTheMemo) {
  for (const auto reason : {ESP_RST_POWERON, ESP_RST_SW}) {
    SdCardFontSystem first, second;
    GfxRenderer r1, r2;
    boot(ESP_RST_POWERON, first, r1);
    boot(reason, second, r2);
    EXPECT_EQ(hostDiscoveries, 1) << reason;
  }
}

TEST_F(SdFontBoot, MemoOfAFileGoneFromTheCardFallsBackToTheWalk) {
  SdCardFontSystem cold, wake;
  GfxRenderer r1, r2;
  boot(ESP_RST_POWERON, cold, r1);
  // While the device slept the card lost the 14 pt file.
  hostCatalog.front() = family("Bokerlam", {12, 16});
  hostCardFiles.erase("/.fonts/Bokerlam/Bokerlam-SD_14.cpfont");
  boot(ESP_RST_DEEPSLEEP, wake, r2);
  EXPECT_EQ(hostDiscoveries, 1);
  EXPECT_EQ(hostLoads.back(), "/.fonts/Bokerlam/Bokerlam-SD_12.cpfont");
  EXPECT_EQ(hostSettings.fontPointSize, 12);
  EXPECT_EQ(std::string(hostSettings.sdFontFamilyName), "Bokerlam");
}

TEST_F(SdFontBoot, FamilyGoneFromTheCardIsClearedAsBefore) {
  SdCardFontSystem cold, wake;
  GfxRenderer r1, r2;
  boot(ESP_RST_POWERON, cold, r1);
  hostCatalog.erase(hostCatalog.begin());
  hostCardFiles.clear();
  boot(ESP_RST_DEEPSLEEP, wake, r2);
  EXPECT_EQ(hostDiscoveries, 1);
  EXPECT_EQ(hostSettings.sdFontFamilyName[0], '\0');
}

TEST_F(SdFontBoot, MemoOfAnotherFamilyIsNotUsed) {
  SdCardFontSystem cold, wake;
  GfxRenderer r1, r2;
  boot(ESP_RST_POWERON, cold, r1);
  std::strcpy(hostSettings.sdFontFamilyName, "Other");
  boot(ESP_RST_DEEPSLEEP, wake, r2);
  EXPECT_EQ(hostDiscoveries, 1);
  EXPECT_EQ(hostLoads.back(), "/fonts/Other/Other-SD_14.cpfont");
}

TEST_F(SdFontBoot, FontsChangedInTheAppDropTheMemo) {
  SdCardFontSystem cold, wake;
  GfxRenderer r1, r2;
  boot(ESP_RST_POWERON, cold, r1);
  cold.markRegistryDirty();
  boot(ESP_RST_DEEPSLEEP, wake, r2);
  EXPECT_EQ(hostDiscoveries, 1);
}

TEST_F(SdFontBoot, CjkFamilyGivesTheSameUiFallbacksFromTheMemo) {
  hostCjk = true;
  SdCardFontSystem cold, wake;
  GfxRenderer r1, r2;
  boot(ESP_RST_POWERON, cold, r1);
  const std::map<int, int> expected{{SMALL_FONT_ID, 108}, {UI_10_FONT_ID, 110}, {UI_12_FONT_ID, 112}};
  EXPECT_EQ(r1.fallbacks, expected);
  boot(ESP_RST_DEEPSLEEP, wake, r2);
  EXPECT_EQ(r2.fallbacks, expected);
  // The boot applies the saved UI text size right after (applyUiFontSize), still from the memo.
  wake.refreshUiFallbacks(r2, 1);
  const std::map<int, int> larger{{SMALL_FONT_ID, 110}, {UI_10_FONT_ID, 112}, {UI_12_FONT_ID, 114}};
  EXPECT_EQ(r2.fallbacks, larger);
  EXPECT_EQ(hostDiscoveries, 0);
}

TEST(SdFontMemo, KeepsOneFamilyExactly) {
  SdCardFontFamilyInfo info;
  info.name = "Bokerlam";
  info.stems = {"Bokerlam-SD"};
  info.hiddenRoot = false;
  info.files = {{16, 0, 0}, {12, 0, 0}, {14, 0, 0}};
  sdfontmemo::Memo memo{};
  ASSERT_TRUE(sdfontmemo::save(info, memo));
  SdCardFontFamilyInfo back;
  ASSERT_TRUE(sdfontmemo::restore(memo, true, "Bokerlam", back));
  EXPECT_EQ(back.name, info.name);
  EXPECT_EQ(back.stems, info.stems);
  EXPECT_EQ(back.hiddenRoot, info.hiddenRoot);
  ASSERT_EQ(back.files.size(), 3u);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(back.files[i].pointSize, info.files[i].pointSize);
    EXPECT_EQ(back.files[i].style, 0);
    EXPECT_EQ(back.files[i].stem, 0);
  }
  EXPECT_FALSE(sdfontmemo::restore(memo, false, "Bokerlam", back));
  EXPECT_FALSE(sdfontmemo::restore(memo, true, "Bokerla", back));
  EXPECT_FALSE(sdfontmemo::restore(memo, true, "Bokerlamm", back));
}

// The memo is only read on a wake from deep sleep, after the boot before it rewrote it, so it is
// not checksummed. Whatever it says is checked where it matters: the tag, the family name and the
// bounds here, the file names by the load itself (MemoOfAFileGoneFromTheCardFallsBackToTheWalk).
TEST(SdFontMemo, WrongTagNameOrBoundsIsNoMemo) {
  SdCardFontFamilyInfo info;
  info.name = "Bokerlam";
  info.stems = {"Bokerlam-SD"};
  info.files = {{14, 0, 0}};
  sdfontmemo::Memo memo{};
  ASSERT_TRUE(sdfontmemo::save(info, memo));
  SdCardFontFamilyInfo back;
  auto broken = memo;
  broken.magic ^= 1;
  EXPECT_FALSE(sdfontmemo::restore(broken, true, "Bokerlam", back));
  broken = memo;
  broken.name[1] ^= 0x10;
  EXPECT_FALSE(sdfontmemo::restore(broken, true, "Bokerlam", back));
  for (const uint8_t count : {uint8_t{0}, uint8_t{sdfontmemo::MAX_SIZES + 1}, uint8_t{255}}) {
    broken = memo;
    broken.count = count;
    EXPECT_FALSE(sdfontmemo::restore(broken, true, "Bokerlam", back)) << int(count);
  }
  broken = memo;
  memset(broken.name, 'x', sizeof(broken.name));
  EXPECT_FALSE(sdfontmemo::restore(broken, true, "Bokerlam", back));
  broken = memo;
  memset(broken.stem, 'x', sizeof(broken.stem));
  EXPECT_FALSE(sdfontmemo::restore(broken, true, "Bokerlam", back));
  sdfontmemo::Memo zero{};
  EXPECT_FALSE(sdfontmemo::restore(zero, true, "", back));
}

TEST(SdFontMemo, FamiliesItCannotDescribeAreNotKept) {
  SdCardFontFamilyInfo two;
  two.name = "Two";
  two.stems = {"A", "B"};
  two.files = {{14, 0, 0}, {16, 0, 1}};
  SdCardFontFamilyInfo many;
  many.name = "Many";
  many.stems = {"M"};
  for (uint8_t size = 1; size <= sdfontmemo::MAX_SIZES + 1; ++size) many.files.push_back({size, 0, 0});
  SdCardFontFamilyInfo longName;
  longName.name = std::string(40, 'n');
  longName.stems = {"L"};
  longName.files = {{14, 0, 0}};
  SdCardFontFamilyInfo none;
  none.name = "None";
  none.stems = {"N"};
  for (const auto* info : {&two, &many, &longName, &none}) {
    sdfontmemo::Memo memo{};
    EXPECT_FALSE(sdfontmemo::save(*info, memo)) << info->name;
    SdCardFontFamilyInfo back;
    EXPECT_FALSE(sdfontmemo::restore(memo, true, info->name.c_str(), back)) << info->name;
  }
}
