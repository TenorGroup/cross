#include <gtest/gtest.h>

#include <SdCardFontManager.h>
#include <esp_system.h>

#include <cstddef>
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

TEST_F(SdFontBoot, LongVariantNamesStaySelectedAfterWake) {
  for (const char* name : {"SP3 - Traveling Typewriter-BOLD1", "SP3 - Traveling Typewriter-BOLD2"}) {
    SdCardFontFamilyInfo info;
    info.name = name;
    info.stems = {"R"};
    info.files = {{14, 0, 0}};
    putOnCard(info);
    std::strcpy(hostSettings.sdFontFamilyName, name);
    SdCardFontSystem cold;
    GfxRenderer r1;
    boot(ESP_RST_POWERON, cold, r1);
    ASSERT_STREQ(hostSettings.sdFontFamilyName, name);
    SdCardFontSystem wake;
    GfxRenderer r2;
    boot(ESP_RST_DEEPSLEEP, wake, r2);
    EXPECT_EQ(hostDiscoveries, 0);
    EXPECT_EQ(hostLoads, std::vector<std::string>{std::string("/.fonts/") + name + "/R_14.cpfont"});
    EXPECT_STREQ(hostSettings.sdFontFamilyName, name);
  }
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
  // The first screen that lists fonts finds the catalog the boot before read.
  EXPECT_EQ(wake.registry().getFamilyCount(), 2);
  EXPECT_EQ(hostDiscoveries, 0);
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
  // The family memo names Bokerlam; the catalog kept with it still has Other.
  EXPECT_EQ(hostDiscoveries, 0);
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

// After a wake the text settings, the font size and the reader menu list every family again. The
// X3 walked its 31 family folders for that on the first screen after every wake (~300 ms); the
// catalog the last walk read now sleeps in RTC memory with the family memo.
TEST_F(SdFontBoot, WakeListsTheFontsWithoutWalkingTheCard) {
  SdCardFontSystem cold;
  GfxRenderer r1;
  boot(ESP_RST_POWERON, cold, r1);
  ASSERT_EQ(hostDiscoveries, 1);
  for (int wakeCount = 0; wakeCount < 3; ++wakeCount) {
    SdCardFontSystem wake;
    GfxRenderer renderer;
    boot(ESP_RST_DEEPSLEEP, wake, renderer);
    EXPECT_EQ(wake.registry().getFamilyCount(), 2) << wakeCount;
    const auto* family = wake.registry().findFamily("Bokerlam");
    ASSERT_NE(family, nullptr);
    std::vector<uint8_t> sizes;
    for (const auto& file : family->files) sizes.push_back(file.pointSize);
    EXPECT_EQ(sizes, (std::vector<uint8_t>{8, 10, 12, 14, 16, 18}));
    hostSettings.fontPointSize = wakeCount % 2 ? 14 : 16;
    wake.ensureLoaded(renderer);
    EXPECT_EQ(wake.resolveFontId("Bokerlam", hostSettings.fontPointSize), 42);
    EXPECT_EQ(hostDiscoveries, 0) << wakeCount;
  }
}

// A catalog first read after the boot (no SD family saved) is kept all the same.
TEST_F(SdFontBoot, CatalogReadOnFirstUseIsKeptForTheWake) {
  hostSettings.sdFontFamilyName[0] = '\0';
  SdCardFontSystem cold, wake;
  GfxRenderer r1, r2;
  boot(ESP_RST_POWERON, cold, r1);
  EXPECT_EQ(cold.registry().getFamilyCount(), 2);
  boot(ESP_RST_DEEPSLEEP, wake, r2);
  EXPECT_EQ(wake.registry().getFamilyCount(), 2);
  EXPECT_EQ(hostDiscoveries, 0);
}

// Fonts sent or downloaded in the app mark the catalog dirty: the next wake walks the card once,
// and the walk the app runs for the new list is what the wake after it keeps.
TEST_F(SdFontBoot, FontsChangedInTheAppWalkTheCardOnceMore) {
  SdCardFontSystem cold;
  GfxRenderer r1;
  boot(ESP_RST_POWERON, cold, r1);
  cold.markRegistryDirty();
  {
    SdCardFontSystem wake;
    GfxRenderer r2;
    boot(ESP_RST_DEEPSLEEP, wake, r2);
    EXPECT_EQ(wake.registry().getFamilyCount(), 2);
    EXPECT_EQ(hostDiscoveries, 1);
    putOnCard(family("Sent", {14, 16}));
    wake.markRegistryDirty();
    wake.refreshIfDirty();
    EXPECT_EQ(wake.registry().getFamilyCount(), 3);
  }
  SdCardFontSystem later;
  GfxRenderer r3;
  boot(ESP_RST_DEEPSLEEP, later, r3);
  EXPECT_EQ(later.registry().getFamilyCount(), 3);
  EXPECT_NE(later.registry().findFamily("Sent"), nullptr);
  EXPECT_EQ(hostDiscoveries, 0);
}

// A family the kept catalog lists but the card lost while the device slept (card taken out)
// walks the card before the setting is dropped, as the boot before the catalog memo did.
TEST_F(SdFontBoot, KeptCatalogThatNoLongerLoadsWalksTheCard) {
  SdCardFontSystem cold, wake;
  GfxRenderer r1, r2;
  boot(ESP_RST_POWERON, cold, r1);
  std::strcpy(hostSettings.sdFontFamilyName, "Other");
  hostCatalog.pop_back();
  hostCatalog.push_back(family("Other", {12}, false));
  hostCardFiles.erase("/fonts/Other/Other-SD_14.cpfont");
  hostCardFiles.insert("/fonts/Other/Other-SD_12.cpfont");
  boot(ESP_RST_DEEPSLEEP, wake, r2);
  EXPECT_EQ(hostDiscoveries, 1);
  EXPECT_EQ(hostLoads.back(), "/fonts/Other/Other-SD_12.cpfont");
  EXPECT_EQ(std::string(hostSettings.sdFontFamilyName), "Other");
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

TEST(SdFontMemo, KeepsLongNamesAndRejectsThePreviousLayout) {
  for (const auto& name : {std::string("SP3 - Traveling Typewriter-BOLD1"),
                           std::string("SP3 - Traveling Typewriter-BOLD2"), std::string(63, 'n')}) {
    SdCardFontFamilyInfo info;
    info.name = name;
    info.stems = {"R"};
    info.files = {{14, 0, 0}};
    sdfontmemo::Memo memo{};
    ASSERT_TRUE(sdfontmemo::save(info, memo));
    SdCardFontFamilyInfo back;
    ASSERT_TRUE(sdfontmemo::restore(memo, true, name.c_str(), back));
    EXPECT_EQ(back.name, name);
    EXPECT_EQ(back.stems, info.stems);
    // Layout 1 had a 32-byte name. Layout 2 has a 64-byte name.
    memo.magic = 0x53464D31u;
    SdCardFontFamilyInfo old;
    EXPECT_FALSE(sdfontmemo::restore(memo, true, name.c_str(), old));
    EXPECT_TRUE(old.name.empty());
    EXPECT_TRUE(old.files.empty());
  }
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
  longName.name = std::string(64, 'n');
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

namespace {
// The X3 card of 26/09: 31 families, names as long as the real ones, eight sizes each, one of
// them with two file stems.
std::vector<SdCardFontFamilyInfo> thirtyOneFamilies() {
  static const char* const names[] = {
      "Alegreya", "AtkinsonHyperlegibleNext", "BeVietnamPro", "Bitter", "Bokerlam", "BokerlamSans",
      "Bookerly", "DavidLibre", "Domitian", "Futura", "IBMPlexSerif", "Inter", "Literata", "Lora",
      "Merriweather", "NotoSansCJKsc", "NotoSerif", "NotoSerifCJKsc", "OpenDyslexic", "PTSerif",
      "Palatino", "RobotoSlab", "SourceSerif4", "SpectralLight", "Charis", "CrimsonPro",
      "EBGaramond", "Gentium", "LibreBaskerville", "Newsreader", "Vollkorn"};
  std::vector<SdCardFontFamilyInfo> out;
  for (const char* name : names) {
    SdCardFontFamilyInfo info;
    info.name = name;
    info.stems = {name};
    info.hiddenRoot = std::string(name) != "Futura";
    for (uint8_t size = 12; size <= 26; size += 2) info.files.push_back({size, 0, 0});
    if (info.name == "Bokerlam") {
      info.stems.push_back("Bokerlam-SD");
      info.files.push_back({8, 0, 1});
    }
    out.push_back(info);
  }
  return out;
}

void expectSameCatalog(const std::vector<SdCardFontFamilyInfo>& a, const std::vector<SdCardFontFamilyInfo>& b) {
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].name, b[i].name);
    EXPECT_EQ(a[i].stems, b[i].stems);
    EXPECT_EQ(a[i].hiddenRoot, b[i].hiddenRoot);
    ASSERT_EQ(a[i].files.size(), b[i].files.size()) << a[i].name;
    for (size_t f = 0; f < a[i].files.size(); ++f) {
      EXPECT_EQ(a[i].files[f].pointSize, b[i].files[f].pointSize);
      EXPECT_EQ(a[i].files[f].stem, b[i].files[f].stem);
      EXPECT_EQ(a[i].files[f].style, b[i].files[f].style);
    }
  }
}
}  // namespace

TEST(SdFontCatalogMemo, KeepsThe31FamilyCardExactly) {
  const auto catalog = thirtyOneFamilies();
  sdfontmemo::Catalog memo{};
  ASSERT_TRUE(sdfontmemo::saveCatalog(catalog, memo));
  std::vector<SdCardFontFamilyInfo> back;
  ASSERT_TRUE(sdfontmemo::restoreCatalog(memo, true, back));
  expectSameCatalog(catalog, back);
  back.clear();
  EXPECT_FALSE(sdfontmemo::restoreCatalog(memo, false, back));
  EXPECT_TRUE(back.empty());
}

// Unlike the one-family memo, a wrong catalog is not caught by a load, so it carries a checksum:
// a flipped bit anywhere, a wrong tag or length, or zeroed memory is no catalog.
TEST(SdFontCatalogMemo, DamagedMemoIsNoCatalog) {
  sdfontmemo::Catalog memo{};
  ASSERT_TRUE(sdfontmemo::saveCatalog(thirtyOneFamilies(), memo));
  std::vector<SdCardFontFamilyInfo> back;
  const size_t used = offsetof(sdfontmemo::Catalog, data) + memo.bytes;  // bytes past it are unused
  for (size_t i = 0; i < used; i += 3) {
    auto broken = memo;
    reinterpret_cast<uint8_t*>(&broken)[i] ^= 0x04;
    back.clear();
    EXPECT_FALSE(sdfontmemo::restoreCatalog(broken, true, back)) << i;
  }
  sdfontmemo::Catalog zero{};
  EXPECT_FALSE(sdfontmemo::restoreCatalog(zero, true, back));
  auto longer = memo;
  longer.bytes = sizeof(memo.data) + 1;
  EXPECT_FALSE(sdfontmemo::restoreCatalog(longer, true, back));
}

// A catalog too big for its RTC space is not kept: the wake walks the card as before.
TEST(SdFontCatalogMemo, CatalogBeyondItsSpaceIsNotKept) {
  std::vector<SdCardFontFamilyInfo> many;
  for (int i = 0; i < 128; ++i) {
    SdCardFontFamilyInfo info;
    info.name = "Audit" + std::to_string(1000 + i);
    info.stems = {info.name};
    for (uint8_t size = 8; size <= 30; size += 2) info.files.push_back({size, 0, 0});
    many.push_back(info);
  }
  sdfontmemo::Catalog memo{};
  EXPECT_FALSE(sdfontmemo::saveCatalog(many, memo));
  std::vector<SdCardFontFamilyInfo> back;
  EXPECT_FALSE(sdfontmemo::restoreCatalog(memo, true, back));
}

// A book has no use for the catalog list. Released when the book opens, the next registry use
// (a font menu) brings it back from the RTC memo the walk left, without walking the card again.
TEST_F(SdFontBoot, ReleasedCatalogComesBackWithoutWalkingTheCard) {
  SdCardFontSystem system;
  GfxRenderer renderer;
  boot(ESP_RST_POWERON, system, renderer);
  ASSERT_EQ(hostDiscoveries, 1);
  EXPECT_TRUE(system.releaseCatalog());
  EXPECT_EQ(system.registry().getFamilyCount(), 2);
  const auto* family = system.registry().findFamily("Bokerlam");
  ASSERT_NE(family, nullptr);
  EXPECT_EQ(family->files.size(), 6u);
  EXPECT_EQ(hostDiscoveries, 1);
  system.ensureLoaded(renderer);
  EXPECT_EQ(system.resolveFontId("Bokerlam", hostSettings.fontPointSize), 42);
}

// A catalog the memo cannot bring back stays: fonts changed in the app, or never read yet.
TEST_F(SdFontBoot, CatalogTheMemoCannotBringBackIsKept) {
  SdCardFontSystem system;
  GfxRenderer renderer;
  boot(ESP_RST_POWERON, system, renderer);
  system.markRegistryDirty();
  EXPECT_FALSE(system.releaseCatalog());

  hostSettings.sdFontFamilyName[0] = '\0';
  SdCardFontSystem unread;
  GfxRenderer r2;
  boot(ESP_RST_POWERON, unread, r2);
  EXPECT_FALSE(unread.releaseCatalog());
  EXPECT_EQ(unread.registry().getFamilyCount(), 2);
  EXPECT_EQ(hostDiscoveries, 1);
}

// After a wake the boot memo's family answered for the catalog until its first read. A released
// catalog must not bring that old family back: a size added since the wake would snap away.
TEST_F(SdFontBoot, ReleasedCatalogDoesNotAnswerWithTheWakeFamily) {
  SdCardFontSystem cold, wake;
  GfxRenderer r1, renderer;
  boot(ESP_RST_POWERON, cold, r1);
  boot(ESP_RST_DEEPSLEEP, wake, renderer);
  hostCatalog[0].files.push_back({20, 0, 0});
  hostCardFiles.insert("/.fonts/Bokerlam/Bokerlam-SD_20.cpfont");
  wake.markRegistryDirty();
  wake.refreshIfDirty();
  hostSettings.fontPointSize = 20;
  wake.ensureLoaded(renderer);
  ASSERT_EQ(hostLoads.back(), "/.fonts/Bokerlam/Bokerlam-SD_20.cpfont");
  ASSERT_TRUE(wake.releaseCatalog());
  hostLoads.clear();
  wake.ensureLoaded(renderer);
  EXPECT_EQ(hostSettings.fontPointSize, 20) << "the released catalog answered with the wake's family";
  EXPECT_TRUE(hostLoads.empty() || hostLoads.back() == "/.fonts/Bokerlam/Bokerlam-SD_20.cpfont");
}
