#include <HalDisplay.h>
#include <HalGPIO.h>
#include <gtest/gtest.h>

#include "SettingsList.h"
#include "MenuFavorites.h"
#include "activities/UiTabListActivity.h"
#include "util/ButtonNavigator.h"

namespace faketest {
extern bool pressed[8];
extern bool released[8];
extern bool held[8];
extern unsigned long heldMs;
void reset();
}  // namespace faketest

HalDisplay& hostTestDisplay();

namespace {

constexpr int kTabCount = 3;
constexpr int kRowCount = 5;

class FakeTabScreen final : public UiTabListActivity {
 public:
  FakeTabScreen(GfxRenderer& renderer, MappedInputManager& input) : UiTabListActivity("FakeTab", renderer, input) {}

  int tab = 0;
  int tabSteps = 0;
  int rows = kRowCount;

  int tabCount() const override { return kTabCount; }
  int activeTab() const override { return tab; }
  const char* tabLabel(int) const override { return "tab"; }
  void onTabAction(int index) override { tab = index; }
  void stepTab(int direction) override {
    tab = (tab + direction + kTabCount) % kTabCount;
    tabSteps++;
  }
  bool handleButtons() override {
    if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return false;
    if (ringPos() == 0) {
      if (listCount() > 0) moveRingTo(1);
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }

  int listCount() const override { return rows; }
  void buildScreen(UiScreen&) override {}
  int activatedRow = -1;
  void activateIndex(int index) override {
    activatedRow = index;
    commitTabNavigation();
  }

  freeink::ui::ListNav& state() { return activeNav(); }

  // Vi tri con tro trong vong: 0 la thanh tab, 1 tro di la cac dong.
  int ring() const { return ringPos(); }
  void tapRow(int index) {
    freeink::ui::ActionEvent event;
    event.value = index;
    onRowAction(event);
  }
};

struct TabScreenFixture : public ::testing::Test {
  HalGPIO gpio;
  GfxRenderer renderer{hostTestDisplay()};
  MappedInputManager input{gpio, renderer};
  FakeTabScreen screen{renderer, input};

  void SetUp() override {
    // Tren may that main.cpp gan con tro tinh nay. Thieu no thi ButtonNavigator
    // thay mappedInput == nullptr va NUOT sach moi lan bam, nen bai kiem do ma
    // khong phai vi hanh vi dang xet.
    ButtonNavigator::setMappedInputManager(input);
    faketest::reset();
    screen.onEnter();
  }

  // Mot lan bam nhanh: mot luot doc thay nhan, luot sau thay nha.
  void tap(const uint8_t hardwareButton) {
    faketest::reset();
    faketest::pressed[hardwareButton] = true;
    screen.loop();
    faketest::reset();
    faketest::released[hardwareButton] = true;
    screen.loop();
    faketest::reset();
  }
};

TEST_F(TabScreenFixture, EveryTabStartsOnItsFirstRow) {
  EXPECT_EQ(screen.ring(), 1);
  tap(HalGPIO::BTN_DOWN);
  EXPECT_EQ(screen.ring(), 1);
  tap(HalGPIO::BTN_DOWN);
  EXPECT_EQ(screen.ring(), 1);
}

TEST_F(TabScreenFixture, HoldFrontMovesToVisibleBoundaryAndUnknownTabGroupDoesNotJump) {
  const auto hold = [this](uint8_t b) {
    faketest::reset();
    faketest::held[b] = true;
    faketest::heldMs = 1200;
    input.update();
    screen.loop();
    screen.loop();
    faketest::reset();
    faketest::released[b] = true;
    input.update();
    if (!input.consumeSuppressedRelease()) screen.loop();
    faketest::reset();
    input.update();
  };
  screen.state().top = 1;
  screen.state().visibleRows = 2;
  screen.state().drawnRows = 2;
  screen.state().drawnCount = kRowCount;
  screen.state().followOnBuild = false;
  hold(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(screen.ring(), 3);
  EXPECT_EQ(screen.state().top, 1);
  hold(HalGPIO::BTN_LEFT);
  EXPECT_EQ(screen.ring(), 2);
  EXPECT_EQ(screen.state().top, 1);
  hold(HalGPIO::BTN_DOWN);
  EXPECT_EQ(screen.tab, 0);
  hold(HalGPIO::BTN_UP);
  EXPECT_EQ(screen.tab, 0);
}

// Tren X3 hai nut canh mang ten lo gic Up/Down nhung nam ben trai va ben phai
// than may. Chung nhay tab.
TEST_F(TabScreenFixture, NutCanhNhayTab) {
  const int before = screen.tab;
  tap(HalGPIO::BTN_DOWN);
  EXPECT_EQ(screen.tab, (before + 1) % kTabCount) << "nut canh phai phai sang tab sau";
  tap(HalGPIO::BTN_UP);
  EXPECT_EQ(screen.tab, before) << "nut canh trai phai quay lai tab truoc";
}

// Cap nut mat truoc mang ten lo gic Left/Right nhung giao dien dan nhan Len/Xuong.
// Chung di giua cac dong, KHONG duoc dong toi tab.
TEST_F(TabScreenFixture, NutMatTruocDiGiuaCacDongChuKhongNhayTab) {
  const int tabBefore = screen.tab;
  const int ringBefore = screen.ring();
  tap(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(screen.tab, tabBefore) << "nut mat truoc khong duoc nhay tab";
  EXPECT_NE(screen.ring(), ringBefore) << "nut mat truoc phai doi dong dang chon";
}

TEST_F(TabScreenFixture, NutMatTruocQuayTrongCacDongKhongVaoThanhThe) {
  for (int i = 1; i < kRowCount; ++i) tap(HalGPIO::BTN_RIGHT);
  ASSERT_EQ(screen.ring(), kRowCount);

  tap(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(screen.ring(), 1) << "hang cuoi sang tiep phai quay ve hang dau";

  tap(HalGPIO::BTN_LEFT);
  EXPECT_EQ(screen.ring(), kRowCount) << "hang dau lui lai phai quay ve hang cuoi";
}

TEST_F(TabScreenFixture, ChonSauBoundaryKichHoatHangDau) {
  for (int i = 1; i < kRowCount; ++i) tap(HalGPIO::BTN_RIGHT);
  tap(HalGPIO::BTN_RIGHT);
  ASSERT_EQ(screen.ring(), 1);

  tap(HalGPIO::BTN_CONFIRM);
  EXPECT_EQ(screen.activatedRow, 0) << "Confirm sau boundary phai mo hang dau";
}

TEST_F(TabScreenFixture, DanhSachRongGiuiConTroOThanhThe) {
  screen.rows = 0;
  tap(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(screen.ring(), 0);
  tap(HalGPIO::BTN_LEFT);
  EXPECT_EQ(screen.ring(), 0);
}

TEST_F(TabScreenFixture, ShrinkingRowsClampsBeforeConfirm) {
  screen.state().selected = kRowCount;
  screen.rows = 2;
  tap(HalGPIO::BTN_CONFIRM);
  EXPECT_EQ(screen.ring(), 2);
  EXPECT_EQ(screen.activatedRow, 1);
}

TEST_F(TabScreenFixture, EmptyTabRegainingRowsSelectsFirstBeforeConfirm) {
  screen.rows = 0;
  screen.loop();
  ASSERT_EQ(screen.ring(), 0);
  screen.rows = 2;
  tap(HalGPIO::BTN_CONFIRM);
  EXPECT_EQ(screen.ring(), 1);
  EXPECT_EQ(screen.activatedRow, 0);
}

TEST_F(TabScreenFixture, RestoredCursorClampsToCurrentRows) {
  MenuNavigationState saved;
  screen.state().selected = kRowCount;
  screen.captureNavigation(saved);
  screen.rows = 2;
  screen.restoreNavigation(saved);
  EXPECT_EQ(screen.ring(), 2);
}

TEST_F(TabScreenFixture, HaiTrucKhongDamNhau) {
  const int stepsBefore = screen.tabSteps;
  tap(HalGPIO::BTN_LEFT);
  tap(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(screen.tabSteps, stepsBefore) << "nut mat truoc khong duoc goi stepTab lan nao";
}

TEST_F(TabScreenFixture, BamNhamNhayTabRoiQuayLaiThiVanConCho) {
  tap(HalGPIO::BTN_RIGHT);
  tap(HalGPIO::BTN_RIGHT);
  const int cho = screen.ring();
  ASSERT_EQ(cho, 3) << "dat con tro xuong dong thu hai truoc da";

  tap(HalGPIO::BTN_DOWN);  // bam nham nut canh, sang tab ben
  tap(HalGPIO::BTN_UP);    // quay lai tab cu

  EXPECT_EQ(screen.ring(), cho) << "quay lai tab cu phai thay dung cho dang dung";
}

TEST_F(TabScreenFixture, MovingFocusInAnotherTabKeepsThePreviousPosition) {
  tap(HalGPIO::BTN_RIGHT);
  tap(HalGPIO::BTN_RIGHT);
  ASSERT_EQ(screen.ring(), 3);

  tap(HalGPIO::BTN_DOWN);   // sang tab ben
  tap(HalGPIO::BTN_RIGHT);  // va bam con tro vao mot dong o day
  tap(HalGPIO::BTN_UP);     // quay lai tab cu

  EXPECT_EQ(screen.ring(), 3) << "moving focus alone must keep the previous position";
}

TEST_F(TabScreenFixture, TouchingARowCommitsTheNewTab) {
  tap(HalGPIO::BTN_RIGHT);
  tap(HalGPIO::BTN_RIGHT);
  ASSERT_EQ(screen.ring(), 3);
  tap(HalGPIO::BTN_DOWN);
  screen.tapRow(2);
  EXPECT_EQ(screen.ring(), 3);
  tap(HalGPIO::BTN_UP);
  EXPECT_EQ(screen.ring(), 1);
}

}  // namespace

namespace {
class FakePagedList final : public UiListActivity {
 public:
  FakePagedList(GfxRenderer& r, MappedInputManager& i) : UiListActivity("PagedList", r, i) {}
  int count = 2088;
  int listCount() const override { return count; }
  void buildScreen(UiScreen&) override {}
  void activateIndex(int) override {}
  freeink::ui::ListNav& state() { return nav; }
};
struct PagedListFixture : public ::testing::Test {
  HalGPIO gpio;
  GfxRenderer renderer{hostTestDisplay()};
  MappedInputManager input{gpio, renderer};
  FakePagedList screen{renderer, input};
  void SetUp() override {
    faketest::reset();
    ButtonNavigator::setMappedInputManager(input);
    screen.onEnter();
    screen.state().visibleRows = 13;
    screen.state().drawnRows = 13;
    screen.state().drawnCount = screen.count;
    screen.state().followOnBuild = false;
  }
  void release(uint8_t b) {
    faketest::reset();
    faketest::released[b] = true;
    input.update();
    if (!input.consumeSuppressedRelease()) screen.loop();
    faketest::reset();
    input.update();
  }
  void hold(uint8_t b) {
    faketest::reset();
    faketest::held[b] = true;
    faketest::heldMs = 1000;
    input.update();
    screen.loop();
  }
};
TEST_F(PagedListFixture, EdgeButtonsMoveTheWholeViewportBackAndForward) {
  screen.state().top = 2075;
  screen.state().selected = 2080;
  release(HalGPIO::BTN_UP);
  EXPECT_EQ(screen.state().top, 2062);
  EXPECT_GE(screen.state().selected, 2062);
  EXPECT_LT(screen.state().selected, 2075);
  release(HalGPIO::BTN_DOWN);
  EXPECT_EQ(screen.state().top, 2075);
}
TEST_F(PagedListFixture, FrontTapMovesOneRowAndHoldJumpsOnceToBoundary) {
  screen.state().selected = 100;
  screen.state().top = 100;
  release(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(screen.state().selected, 101);
  hold(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(screen.state().selected, 112);
  EXPECT_EQ(screen.state().top, 100);
  hold(HalGPIO::BTN_RIGHT);
  release(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(screen.state().selected, 112);
  hold(HalGPIO::BTN_LEFT);
  release(HalGPIO::BTN_LEFT);
  EXPECT_EQ(screen.state().selected, 100);
  EXPECT_EQ(screen.state().top, 100);
}
TEST_F(PagedListFixture, PageAtBoundaryClampsInsteadOfWrapping) {
  release(HalGPIO::BTN_UP);
  EXPECT_EQ(screen.state().top, 0);
  screen.state().top = 2075;
  screen.state().selected = 2087;
  release(HalGPIO::BTN_DOWN);
  EXPECT_EQ(screen.state().top, 2075);
  EXPECT_EQ(screen.state().selected, 2087);
}
TEST_F(PagedListFixture, WrappedRowsUseRenderedCountInsteadOfEstimate) {
  screen.state().visibleRows = 13;
  screen.state().drawnRows = 7;
  release(HalGPIO::BTN_DOWN);
  EXPECT_EQ(screen.state().top, 7);
  EXPECT_GE(screen.state().selected, 7);
  EXPECT_LT(screen.state().selected, 14);
}
TEST_F(PagedListFixture, EmptyListAndStaleMeasurementAreSafe) {
  screen.count = 0;
  hold(HalGPIO::BTN_RIGHT);
  release(HalGPIO::BTN_RIGHT);
  EXPECT_EQ(screen.state().selected, 0);
  release(HalGPIO::BTN_DOWN);
  EXPECT_EQ(screen.state().top, 0);
  screen.count = 100;
  screen.state().visibleRows = 13;
  screen.state().drawnRows = 2;
  screen.state().drawnCount = 50;
  release(HalGPIO::BTN_DOWN);
  EXPECT_EQ(screen.state().top, 13);
}
}  // namespace

#include "MenuCustomization.h"
TEST(MenuCustomization, NormalizeDropsDuplicatesAndAppendsNewIds) {
  menucustom::State s;
  s.order[1] = {4, 4, 255, 0, 99, 0, 255, 255};
  s.normalize(1, 7);
  EXPECT_EQ(s.order[1], (std::array<uint8_t, 8>{4, 0, 1, 2, 3, 5, 6, 0}));
}
TEST(MenuCustomization, MovingTabRetainsIdAndStopsAtEdge) {
  auto& s = menucustom::state();
  s = menucustom::State{};
  ASSERT_TRUE(menucustom::moveTab(0, 4, 5, -1));
  EXPECT_EQ(menucustom::position(0, 4, 5), 1);
  EXPECT_EQ(menucustom::adjacent(0, 4, 5, 1), 1);
  EXPECT_FALSE(menucustom::moveTab(0, 0, 5, -1));
  EXPECT_EQ(menucustom::position(0, 0, 5), 0);
  s = menucustom::State{};
}
TEST(MenuCustomization, PinsAreUniqueAndEmptyRemainsEmpty) {
  auto& s = menucustom::state();
  s = menucustom::State{};
  ASSERT_TRUE(menucustom::togglePin("clock/clockFormat"));
  ASSERT_EQ(s.pinCount, 1);
  ASSERT_TRUE(menucustom::togglePin("clock/clockFormat"));
  EXPECT_EQ(s.pinCount, 0);
}

namespace menucustom {
extern bool failSave;
}
TEST(MenuCustomization, FailedSaveRollsBackPinsAndOrder) {
  using namespace menucustom;
  state() = State{};
  ASSERT_TRUE(togglePin("settings/sleepScreen"));
  const auto before = state();
  failSave = true;
  EXPECT_FALSE(togglePin("settings/sleepScreen"));
  EXPECT_EQ(state().pinCount, 1);
  EXPECT_EQ(state().find("settings/sleepScreen"), 0);
  EXPECT_FALSE(togglePin("text/fontSize"));
  EXPECT_EQ(state().pinCount, 1);
  EXPECT_FALSE(moveTab(0, 0, 5, 1));
  EXPECT_EQ(state().order, before.order);
  failSave = false;
  ASSERT_TRUE(togglePin("text/fontSize"));
  failSave = true;
  EXPECT_FALSE(movePin("text/fontSize", -1));
  EXPECT_EQ(state().find("text/fontSize"), 1);
  failSave = false;
  state() = State{};
}
TEST(MenuCustomization, BoundsPinsWithoutEvictingExistingEntries) {
  using namespace menucustom;
  state() = State{};
  for (int i = 0; i < MAX_PINS; ++i) ASSERT_TRUE(togglePin(("settings/test" + std::to_string(i)).c_str()));
  EXPECT_FALSE(togglePin("settings/overflow"));
  EXPECT_EQ(state().pinCount, MAX_PINS);
  EXPECT_FALSE(togglePin(std::string(KEY_SIZE, 'x').c_str()));
  EXPECT_EQ(state().pinCount, MAX_PINS);
  ASSERT_TRUE(togglePin("settings/test4"));
  EXPECT_EQ(state().find("settings/test5"), 4);
  EXPECT_EQ(state().pinCount, MAX_PINS - 1);
  state() = State{};
}

TEST(SettingsClockAdapter, ReadsEveryLegacyValueWithoutChangingStorageOrRegistration) {
  const auto registered = SettingInfo::Enum(
      StrId::STR_CLOCK, &CrossPointSettings::statusBarClock,
      {StrId::STR_HIDE, StrId::STR_DIR_RIGHT, StrId::STR_DIR_LEFT}, "statusBarClock", StrId::STR_CUSTOMISE_STATUS_BAR);
  const auto original = SETTINGS.statusBarClock;
  for (const uint8_t legacy : {0, 1, 2}) {
    SETTINGS.statusBarClock = legacy;
    const auto device = buildTenorClockPlacementSetting(registered);
    EXPECT_EQ(device.valueGetter(), legacy == 2 ? 1 : 0);
    EXPECT_EQ(SETTINGS.statusBarClock, legacy);
    EXPECT_EQ(device.enumValues.size(), 2);
    EXPECT_STREQ(device.key, registered.key);
    EXPECT_EQ(registered.valuePtr, &CrossPointSettings::statusBarClock);
    EXPECT_EQ(registered.category, StrId::STR_CUSTOMISE_STATUS_BAR);
    EXPECT_EQ(registered.enumValues.size(), 3);
  }
  SETTINGS.statusBarClock = original;
}

TEST(SettingsClockAdapter, ExplicitSelectionWritesOnlyExistingRightOrLeftEnums) {
  const auto registered = SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock, {}, "statusBarClock");
  const auto device = buildTenorClockPlacementSetting(registered);
  const auto original = SETTINGS.statusBarClock;
  device.valueSetter(0);
  EXPECT_EQ(SETTINGS.statusBarClock, CrossPointSettings::STATUS_BAR_CLOCK_RIGHT);
  device.valueSetter(1);
  EXPECT_EQ(SETTINGS.statusBarClock, CrossPointSettings::STATUS_BAR_CLOCK_LEFT);
  SETTINGS.statusBarClock = original;
}

TEST(SettingsJsonRoundTrip, WakeIntoBookPersistsAndClampsLikeEveryEnumKey) {
  auto& settings = SETTINGS;
  const uint8_t original = settings.wakeIntoBook;

  // Key absent from an older file: the struct default stands.
  settings.wakeIntoBook = 0;
  {
    JsonDocument doc;
    ASSERT_TRUE(settings.fromJson(doc.as<JsonVariantConst>()));
    EXPECT_EQ(settings.wakeIntoBook, 0);
  }

  // A written value survives reading the file back and writing it out again.
  {
    JsonDocument doc;
    doc["wakeIntoBook"] = 1;
    ASSERT_TRUE(settings.fromJson(doc.as<JsonVariantConst>()));
    EXPECT_EQ(settings.wakeIntoBook, 1);

    JsonDocument saved;
    settings.toJson(saved);
    EXPECT_EQ(saved["wakeIntoBook"] | uint8_t{255}, 1);
  }

  // Out-of-range value falls back to the default, the way every enum key does.
  settings.wakeIntoBook = 0;
  {
    JsonDocument doc;
    doc["wakeIntoBook"] = 7;
    ASSERT_TRUE(settings.fromJson(doc.as<JsonVariantConst>()));
    EXPECT_EQ(settings.wakeIntoBook, 0);
  }

  settings.wakeIntoBook = original;
}

// A default clock represents a board whose RTC probe did not find hardware.
HalClock halClock;

TEST(MenuFavoritesCompatibility, RenamedPinsRemainFindableAndCanBeRemovedFromNewRows) {
  const char* oldKeys[] = {"text/focusReadingEnabled", "settings/hideGlobalStatusBar", "settings/hideReaderStatusBar"};
  const char* newKeys[] = {"text/dropCapMode", "settings/globalStatusBarMode", "settings/readerStatusBarMode"};
  for (int i = 0; i < 3; ++i) {
    auto& pins = menucustom::state();
    pins = menucustom::State{};
    ASSERT_TRUE(menucustom::togglePin(oldKeys[i]));
    EXPECT_EQ(pins.find(newKeys[i]), 0);
    EXPECT_STREQ(pins.pins[0].data(), oldKeys[i]);
    ASSERT_TRUE(menucustom::togglePin(newKeys[i]));
    EXPECT_EQ(pins.pinCount, 0) << newKeys[i];
  }
  menucustom::state() = menucustom::State{};
}

TEST(MenuFavoritesCompatibility, LegacyTextPinFindsCurrentRouteAndLabel) {
  const auto* route = menufavorites::find("text/focusReadingEnabled");
  ASSERT_NE(route, nullptr);
  EXPECT_STREQ(route->key, "text/dropCapMode");
  EXPECT_EQ(route->tab, 3);
  EXPECT_EQ(route->row, 0);
  EXPECT_EQ(menufavorites::label("text/focusReadingEnabled", {}), StrId::STR_FOCUS_READING);
}

TEST(MenuFavoritesCompatibility, LegacyStatusPinsShowCurrentLabels) {
  const std::vector<SettingInfo> settings = {
      SettingInfo::Enum(StrId::STR_HIDE_GLOBAL_STATUS_BAR, &CrossPointSettings::globalStatusBarMode, {}, "globalStatusBarMode"),
      SettingInfo::Enum(StrId::STR_HIDE_READER_STATUS_BAR, &CrossPointSettings::readerStatusBarMode, {}, "readerStatusBarMode"),
  };
  EXPECT_EQ(menufavorites::label("settings/hideGlobalStatusBar", settings), StrId::STR_HIDE_GLOBAL_STATUS_BAR);
  EXPECT_EQ(menufavorites::label("settings/hideReaderStatusBar", settings), StrId::STR_HIDE_READER_STATUS_BAR);
}

TEST(MenuFavoritesCompatibility, MissingRtcHidesClockPinBeforeTenorLabelOverride) {
  const auto originalTheme = SETTINGS.uiTheme;
  SETTINGS.uiTheme = CrossPointSettings::TENOR_UI;
  ASSERT_FALSE(halClock.isAvailable());
  EXPECT_EQ(menufavorites::label("status/statusBarClock", {}), StrId::STR_NONE_OPT);
  EXPECT_EQ(menufavorites::label("action/2", {}), StrId::STR_NONE_OPT);
  EXPECT_EQ(menufavorites::label("clock/clockFormat", {}), StrId::STR_NONE_OPT);
  SETTINGS.uiTheme = originalTheme;
}
