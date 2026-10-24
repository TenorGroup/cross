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

// NetworkMode (activities/network/NetworkModeSelectionActivity.h; connect_wake.py holds these numbers to the enum):
// JOIN_NETWORK 0, CONNECT_CALIBRE 1, CREATE_HOTSPOT 2, USB_DRIVE 3, WEB_DAV 4, FONT_STORE 5. The X3 has no USB drive
// and no WebDAV row; the X4 Classic and the X4 Pro have both.
constexpr int JOIN_NETWORK = 0, CREATE_HOTSPOT = 2, USB_DRIVE = 3, WEB_DAV = 4, FONT_STORE = 5;
const auto x3Rows = [](const int mode) { return mode == 0 || mode == 1 || mode == 2 || mode == 5; };
const auto x4Rows = [](const int mode) { return mode >= 0 && mode <= 5; };

WakePlace resolve(const WakePlace& p, const bool deepSleepReset = true, const uint8_t shell = 0,
                  const bool powerButtonStart = false) {
  return wakeplace::resolve(deepSleepReset, powerButtonStart || deepSleepReset, shell, p, recents, onCard, isFolder,
                            x4Rows);
}

WakePlace connect(const int mode, const std::string& book = "", const uint8_t shell = 0) {
  return place(WakeKind::Connect, book, "", 0, mode, shell);
}
}  // namespace

// The caller passes true only when the chip's reset reason is deep sleep: a power-on (power lost, USB), a watchdog,
// a brownout or a panic is a start, and gets Home.
TEST(WakePlace, OnlyAWakeOutOfDeepSleepReopensAnything) {
  // Neither a deep-sleep wake nor a power-button start (a watchdog, a brownout, USB power): nothing, a book neither.
  EXPECT_EQ(resolve(place(WakeKind::Book, "/a.epub"), false).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::Files, "/books"), false).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::SettingsTab, "", "", 2, 3), false).kind, WakeKind::None);
  EXPECT_EQ(resolve(connect(FONT_STORE, "/a.epub"), false).kind, WakeKind::None);
}

// A sleep that lost its power (the X4 Classic lets go of its power latch with no valid clock; any unit on a flat
// battery) comes back as a power-on started by the power button. The book the reader was in still opens, as
// "wake into the book" opens it; the other places keep the deep-sleep rule (founder 24/10/2026: picking up where I left
// off includes being in the book).
TEST(WakePlace, ABookOpensAfterASleepThatLostItsPower) {
  const auto book = resolve(place(WakeKind::Book, "/a.epub"), false, 0, /*powerButtonStart=*/true);
  EXPECT_EQ(book.kind, WakeKind::Book);
  EXPECT_EQ(book.path, "/a.epub");
  EXPECT_EQ(resolve(place(WakeKind::Book, "/gone-from-recent.epub"), false, 0, true).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::Book, "/a.epub"), false, 1, true).kind, WakeKind::None);  // the other shell's
  EXPECT_EQ(resolve(place(WakeKind::Files, "/books"), false, 0, true).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::SettingsTab, "", "", 2, 3), false, 0, true).kind, WakeKind::None);
  EXPECT_EQ(resolve(connect(FONT_STORE, "/a.epub"), false, 0, true).kind, WakeKind::None);
}

TEST(WakePlace, ABookIsReopenedUnderTheWakeIntoBookRules) {
  const auto kept = resolve(place(WakeKind::Book, "/a.epub"));
  EXPECT_EQ(kept.kind, WakeKind::Book);
  EXPECT_EQ(kept.path, "/a.epub");
  EXPECT_EQ(resolve(place(WakeKind::Book, "/gone-from-recent.epub")).kind, WakeKind::None);
  EXPECT_EQ(resolve(place(WakeKind::Book, "")).kind, WakeKind::None);
  const auto gone = [](const std::string&) { return false; };
  EXPECT_EQ(wakeplace::resolve(true, true, 0, place(WakeKind::Book, "/a.epub"), recents, gone, isFolder, x4Rows).kind,
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

// Connections (the list, a session, the font store) is kind 5: appended after every kind v1.0.72 knows, so that build
// reading a state.json this one wrote goes Home (its switch ends in `default`) instead of mistaking it for a place.
TEST(WakePlace, ConnectIsAppendedAfterTheKindsAnOlderBuildKnows) {
  EXPECT_EQ(static_cast<int>(WakeKind::SettingsTab), 4);
  EXPECT_EQ(static_cast<int>(WakeKind::Connect), 5);
}

TEST(WakePlace, AnOlderBuildReadsConnectAsHome) {
  // v1.0.72's wakeplace::resolve, frozen: it knows the kinds up to SettingsTab and sends the rest Home.
  enum class OldKind : uint8_t { None, HomeTab, Book, Files, SettingsTab };
  const auto old = [](const uint8_t kind) {
    switch (static_cast<OldKind>(kind)) {
      case OldKind::Book:
      case OldKind::Files:
      case OldKind::HomeTab:
      case OldKind::SettingsTab:
        return true;
      default:
        return false;
    }
  };
  EXPECT_FALSE(old(static_cast<uint8_t>(WakeKind::Connect)));
  EXPECT_TRUE(old(static_cast<uint8_t>(WakeKind::SettingsTab)));
}

TEST(WakePlace, ConnectionsKeepsTheModeOfTheSessionAndTheBookItCameFrom) {
  for (const int mode : {JOIN_NETWORK, 1, CREATE_HOTSPOT, USB_DRIVE, WEB_DAV, FONT_STORE}) {
    const auto kept = resolve(connect(mode, "/a.epub"));
    EXPECT_EQ(kept.kind, WakeKind::Connect) << mode;
    EXPECT_EQ(kept.row, mode);
    EXPECT_EQ(kept.path, "/a.epub");
  }
  EXPECT_EQ(resolve(connect(FONT_STORE)).path, "");  // opened outside a book
}

TEST(WakePlace, AModeTheBoardHasNoRowForGoesHome) {
  const auto on = [](const auto& rows, const int mode) {
    return wakeplace::resolve(true, true, 0, connect(mode), recents, onCard, isFolder, rows).kind;
  };
  EXPECT_EQ(on(x3Rows, USB_DRIVE), WakeKind::None);
  EXPECT_EQ(on(x3Rows, WEB_DAV), WakeKind::None);
  EXPECT_EQ(on(x3Rows, FONT_STORE), WakeKind::Connect);
  EXPECT_EQ(on(x4Rows, USB_DRIVE), WakeKind::Connect);
  EXPECT_EQ(on(x4Rows, WEB_DAV), WakeKind::Connect);
  EXPECT_EQ(on(x4Rows, -1), WakeKind::None);
  EXPECT_EQ(on(x4Rows, 6), WakeKind::None);
  EXPECT_EQ(on(x4Rows, 99), WakeKind::None);
}

TEST(WakePlace, ABookThatIsGoneLeavesThePlaceWithoutIt) {
  const auto gone = resolve(connect(FONT_STORE, "/gone-from-recent.epub"));
  EXPECT_EQ(gone.kind, WakeKind::Connect);
  EXPECT_EQ(gone.row, FONT_STORE);
  EXPECT_EQ(gone.path, "");  // the list or the store still opens; Back goes Home, not to a book that is not there
  const auto offCard = [](const std::string&) { return false; };
  const auto lost = wakeplace::resolve(true, true, 0, connect(JOIN_NETWORK, "/a.epub"), recents, offCard, isFolder, x4Rows);
  EXPECT_EQ(lost.kind, WakeKind::Connect);
  EXPECT_EQ(lost.path, "");
}

TEST(WakePlace, AConnectionsPlaceOfTheOtherShellMeansNothing) {
  EXPECT_EQ(resolve(connect(FONT_STORE, "", /*shell=*/1), true, 0).kind, WakeKind::None);
  EXPECT_EQ(resolve(connect(FONT_STORE, "", /*shell=*/1), true, 1).kind, WakeKind::Connect);
}
