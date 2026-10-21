// "On wake" = pick up where I left off: what a wake reopens of the place the sleep kept.
#include <gtest/gtest.h>

#include <set>

#include "util/WakePlace.h"

namespace {
struct Book {
  std::string path;
};
const std::vector<Book> recents{{"/a.epub"}, {"/b.epub"}};
const auto onCard = [](const std::string&) { return true; };
const auto folders = std::set<std::string>{"/", "/books", "/books/sf"};
const auto isFolder = [](const std::string& path) { return folders.count(path) != 0; };

WakePlace place(const WakeKind kind, const std::string& path = "", const std::string& name = "", const int tab = 0,
                const int row = 0, const uint8_t shell = 0) {
  WakePlace p;
  p.kind = kind;
  p.shell = shell;
  p.tab = static_cast<int16_t>(tab);
  p.row = static_cast<int16_t>(row);
  p.path = path;
  p.name = name;
  return p;
}

WakePlace resolve(const WakePlace& p, const bool deepSleepReset = true, const uint8_t shell = 0) {
  return wakeplace::resolve(deepSleepReset, shell, p, recents, onCard, isFolder);
}
}  // namespace

// The caller passes true only when the chip's reset reason is deep sleep: a power-on (power lost, USB), a watchdog,
// a brownout or a panic is a start, and gets Home.
TEST(WakePlace, OnlyAWakeOutOfDeepSleepReopensAnything) {
  EXPECT_EQ(resolve(place(WakeKind::Book, "/a.epub"), false).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::Files, "/books"), false).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::SettingsTab, "", "", 2, 3), false).kind, WakeKind::None);
}

TEST(WakePlace, ABookIsReopenedUnderTheWakeIntoBookRules) {
  const auto kept = resolve(place(WakeKind::Book, "/a.epub"));
  EXPECT_EQ(kept.kind, WakeKind::Book);
  EXPECT_EQ(kept.path, "/a.epub");
  EXPECT_EQ(resolve(place(WakeKind::Book, "/gone-from-recent.epub")).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::Book, "")).kind, WakeKind::None);
  const auto gone = [](const std::string&) { return false; };
  EXPECT_EQ(wakeplace::resolve(true, 0, place(WakeKind::Book, "/a.epub"), recents, gone, isFolder).kind,
            WakeKind::None);
}

TEST(WakePlace, AFolderThatIsStillThereKeepsItsEntry) {
  const auto kept = resolve(place(WakeKind::Files, "/books/sf", "dune.epub"));
  EXPECT_EQ(kept.kind, WakeKind::Files);
  EXPECT_EQ(kept.path, "/books/sf");
  EXPECT_EQ(kept.name, "dune.epub");
}

TEST(WakePlace, AFolderThatIsGoneGivesWayToTheNearestOneAbove) {
  const auto up = resolve(place(WakeKind::Files, "/books/sf/old/2019", "x.epub"));
  EXPECT_EQ(up.kind, WakeKind::Files);
  EXPECT_EQ(up.path, "/books/sf");
  EXPECT_EQ(up.name, "");  // the entry belonged to the folder that is gone
  const auto root = resolve(place(WakeKind::Files, "/removed/deeper", "x"));
  EXPECT_EQ(root.path, "/");
  EXPECT_EQ(resolve(place(WakeKind::Files, "/removed")).path, "/");
  EXPECT_EQ(resolve(place(WakeKind::Files, "/", "a.txt")).name, "a.txt");
  EXPECT_EQ(resolve(place(WakeKind::Files, "")).kind, WakeKind::None);
}

TEST(WakePlace, AShellsPlaceMeansNothingToTheOther) {
  EXPECT_EQ(resolve(place(WakeKind::Files, "/books", "", 0, 0, /*shell=*/1), true, 0).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::HomeTab, "", "", 1, 2, /*shell=*/0), true, 1).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::HomeTab, "", "", 1, 2, /*shell=*/1), true, 1).kind, WakeKind::HomeTab);
}

TEST(WakePlace, AKindFromAnotherBuildOrAMangledPlaceGoesHome) {
  EXPECT_EQ(resolve(place(static_cast<WakeKind>(99), "/books")).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::None, "/books")).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::HomeTab, "", "", wakeplace::HOME_TABS)).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::HomeTab, "", "", -1)).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::SettingsTab, "", "", wakeplace::SETTINGS_TABS)).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::SettingsTab, "", "", 1, -5)).kind, WakeKind::None);
}

TEST(WakePlace, ASettingsRowTravelsAsItsKey) {
  const auto kept = resolve(place(WakeKind::SettingsTab, "", "sleepTimeoutMinutes", 7));
  EXPECT_EQ(kept.kind, WakeKind::SettingsTab);
  EXPECT_EQ(kept.name, "sleepTimeoutMinutes");  // found by key when the tab opens; no key found: the first row
}

TEST(WakePlace, ATabAndRowAreKept) {
  const auto home = resolve(place(WakeKind::HomeTab, "", "", 3, 4));
  EXPECT_EQ(home.kind, WakeKind::HomeTab);
  EXPECT_EQ(home.tab, 3);
  EXPECT_EQ(home.row, 4);
  const auto settings = resolve(place(WakeKind::SettingsTab, "", "", wakeplace::SETTINGS_TABS - 1, 7));
  EXPECT_EQ(settings.kind, WakeKind::SettingsTab);
  EXPECT_EQ(settings.row, 7);
}
