#include <HalDisplay.h>
#include <HalGPIO.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <new>

#include "SettingsList.h"
#include "activities/reader/ReaderUtils.h"

// Do cu cap phat LON NHAT trong mot doan ma. Khong dem tong, vi thu giet may la
// mot khoi LIEN NHAU khong xin duoc chu khong phai tong so byte: tren may that
// `operator new` xin 16.560 byte mot cuc, truot, nem bad_alloc, ma du an tat
// ngoai le nen thanh abort(). Coredump 19/09/2026 chi dung vao day.
namespace alloctest {
bool recording = false;
size_t largest = 0;
void reset() { largest = 0; }
}  // namespace alloctest

void* operator new(size_t n) {
  if (alloctest::recording && n > alloctest::largest) alloctest::largest = n;
  void* p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
#include "MenuFavorites.h"
#include "activities/UiTabListActivity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

namespace faketest {
extern bool pressed[8];
extern bool released[8];
extern bool held[8];
extern unsigned long heldMs;
void reset();
}  // namespace faketest

namespace tiltfixture {
extern uint8_t lastMode;
extern bool lastTargetActive;
extern uint8_t lastVerticalMode;
extern bool lastVerticalTargetActive;
void reset();
void injectPhysicalForward();
void injectPhysicalVertical();
}  // namespace tiltfixture

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
  bool modalActive = false;

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
  bool acceptsTiltTabs() const { return acceptsTiltTabNavigation(); }
  bool queueTilt(const bool forward, const bool backward) { return queueTiltTabNavigation(forward, backward); }
  bool acceptsTiltRows() const { return acceptsTiltMenuNavigation(); }
  bool queueTiltRow(const bool up, const bool down) { return queueTiltMenuNavigation(up, down); }
  OptionPopup popup;

 protected:
  bool allowsTiltTabNavigation() const override { return !modalActive && !popup.isActive(); }
  OptionPopup* tiltPopup() override { return &popup; }

 public:

  // Vi tri con tro trong vong: 0 la thanh tab, 1 tro di la cac dong.
  int ring() const { return ringPos(); }
  void tapRow(int index) {
    freeink::ui::ActionEvent event;
    event.value = index;
    onRowAction(event);
  }
};

// A list without tabs: a sub-screen such as the Bluetooth page or a reader's chapter list.
class FakePlainScreen final : public UiListActivity {
 public:
  FakePlainScreen(GfxRenderer& renderer, MappedInputManager& input) : UiListActivity("FakePlain", renderer, input) {}
  int listCount() const override { return kRowCount; }
  void buildScreen(UiScreen&) override {}
  void activateIndex(int) override {}
  int selected() { return activeNav().selected; }
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

TEST_F(TabScreenFixture, TiltQueuesOneTabStepAndKeepsPerTabFocus) {
  tap(HalGPIO::BTN_RIGHT);
  tap(HalGPIO::BTN_RIGHT);
  ASSERT_EQ(screen.ring(), 3);

  const int stepsBefore = screen.tabSteps;
  ASSERT_TRUE(screen.queueTilt(true, false));
  screen.loop();
  EXPECT_EQ(screen.tab, 1);
  EXPECT_EQ(screen.tabSteps, stepsBefore + 1);

  ASSERT_TRUE(screen.queueTilt(false, true));
  screen.loop();
  EXPECT_EQ(screen.tab, 0);
  EXPECT_EQ(screen.ring(), 3);

  const int coalescedSteps = screen.tabSteps;
  ASSERT_TRUE(screen.queueTilt(true, true));
  screen.loop();
  EXPECT_EQ(screen.tabSteps, coalescedSteps + 1);
}

TEST_F(TabScreenFixture, ReaderTiltModeStaysIndependentFromTabTiltMode) {
  SETTINGS.tiltTabNavigation = CrossPointSettings::TILT_NVERTED;
  const uint8_t readerModes[] = {CrossPointSettings::TILT_OFF, CrossPointSettings::TILT_NORMAL,
                                 CrossPointSettings::TILT_NVERTED};

  for (const uint8_t readerMode : readerModes) {
    SCOPED_TRACE(readerMode);
    SETTINGS.tiltPageTurn = readerMode;
    tiltfixture::reset();
    tiltfixture::injectPhysicalForward();
    halTiltSensor.update(SETTINGS.tiltPageTurn, CrossPointOrientation::PORTRAIT, true);

    const auto turn = ReaderUtils::detectPageTurn(input);
    EXPECT_EQ(tiltfixture::lastMode, readerMode);
    EXPECT_TRUE(tiltfixture::lastTargetActive);
    EXPECT_EQ(turn.next, readerMode == CrossPointSettings::TILT_NORMAL);
    EXPECT_EQ(turn.prev, readerMode == CrossPointSettings::TILT_NVERTED);
    EXPECT_EQ(turn.fromTilt, readerMode != CrossPointSettings::TILT_OFF);
  }
}

TEST_F(TabScreenFixture, TabTiltModeStaysIndependentFromReaderTiltMode) {
  SETTINGS.tiltPageTurn = CrossPointSettings::TILT_NORMAL;
  const uint8_t tabModes[] = {CrossPointSettings::TILT_OFF, CrossPointSettings::TILT_NORMAL,
                              CrossPointSettings::TILT_NVERTED};

  for (const uint8_t tabMode : tabModes) {
    SCOPED_TRACE(tabMode);
    SETTINGS.tiltTabNavigation = tabMode;
    screen.tab = 0;
    screen.tabSteps = 0;
    tiltfixture::reset();
    tiltfixture::injectPhysicalForward();
    screen.loop();

    EXPECT_EQ(tiltfixture::lastMode, tabMode);
    EXPECT_TRUE(tiltfixture::lastTargetActive);
    EXPECT_EQ(screen.tabSteps, tabMode == CrossPointSettings::TILT_OFF ? 0 : 1);
    // Measured on the X3 22/09: with the readings swapped, the mode readers
    // called Inverted was the natural one. Normal now steps next again.
    EXPECT_EQ(screen.tab, tabMode == CrossPointSettings::TILT_NORMAL ? 1
                                                                       : tabMode == CrossPointSettings::TILT_NVERTED ? 2 : 0);
  }
}

TEST_F(TabScreenFixture, ModalAndInactiveTargetsRejectTiltEvents) {
  EXPECT_TRUE(screen.acceptsTiltTabs());
  screen.modalActive = true;
  EXPECT_FALSE(screen.acceptsTiltTabs());
  EXPECT_FALSE(screen.queueTilt(true, false));

  EXPECT_FALSE(HalTiltSensor::shouldDiscardPendingEvents(CrossPointTiltPageTurn::TILT_NORMAL, true));
  EXPECT_TRUE(HalTiltSensor::shouldDiscardPendingEvents(CrossPointTiltPageTurn::TILT_NORMAL, false));
  EXPECT_TRUE(HalTiltSensor::shouldDiscardPendingEvents(CrossPointTiltPageTurn::TILT_OFF, true));
}

// Nghieng doc: mot cu dong di dung MOT dong, y nhu bam nut len hoac xuong.
TEST_F(TabScreenFixture, NghiengDocDiMotDongNhuNutLenXuong) {
  SETTINGS.tiltTabNavigation = CrossPointSettings::TILT_OFF;
  SETTINGS.tiltMenuNavigation = CrossPointSettings::TILT_NORMAL;
  ASSERT_EQ(screen.ring(), 1);

  tiltfixture::reset();
  tiltfixture::injectPhysicalVertical();
  screen.loop();
  EXPECT_EQ(tiltfixture::lastVerticalMode, CrossPointSettings::TILT_NORMAL);
  EXPECT_TRUE(tiltfixture::lastVerticalTargetActive);
  // Do tren X3 22/09: cu dong nay o muc Binh thuong phai di XUONG mot dong, dung nhu nut.
  EXPECT_EQ(screen.ring(), 2);
  EXPECT_EQ(screen.tabSteps, 0) << "nghieng doc khong duoc cham toi tab";

  SETTINGS.tiltMenuNavigation = CrossPointSettings::TILT_NVERTED;
  tiltfixture::reset();
  tiltfixture::injectPhysicalVertical();
  screen.loop();
  EXPECT_EQ(screen.ring(), 1) << "Dao chieu phai di nguoc lai dung mot dong";
}

TEST_F(TabScreenFixture, NghiengDocTatThiKhongDoiDong) {
  SETTINGS.tiltMenuNavigation = CrossPointSettings::TILT_OFF;
  const int before = screen.ring();
  tiltfixture::reset();
  tiltfixture::injectPhysicalVertical();
  screen.loop();
  EXPECT_EQ(tiltfixture::lastVerticalMode, CrossPointSettings::TILT_OFF);
  EXPECT_EQ(screen.ring(), before);
}

// Hai truc chay song song: nghieng ngang van nhay tab nhu cu, nghieng doc di dong.
TEST_F(TabScreenFixture, NghiengNgangVanNhayTabKhiNghiengDocDangBat) {
  SETTINGS.tiltTabNavigation = CrossPointSettings::TILT_NORMAL;
  SETTINGS.tiltMenuNavigation = CrossPointSettings::TILT_NORMAL;
  screen.tab = 0;
  screen.tabSteps = 0;
  const int ringBefore = screen.ring();

  tiltfixture::reset();
  tiltfixture::injectPhysicalForward();
  screen.loop();
  EXPECT_EQ(tiltfixture::lastMode, CrossPointSettings::TILT_NORMAL);
  EXPECT_EQ(screen.tabSteps, 1) << "nghieng ngang van phai nhay dung mot tab";
  EXPECT_EQ(screen.tab, 1) << "muc Binh thuong nhay toi tab ke, do tren X3 22/09";
  EXPECT_EQ(screen.ring(), ringBefore) << "nghieng ngang khong duoc doi dong";
}

TEST_F(TabScreenFixture, ModalChanNghiengDoc) {
  SETTINGS.tiltMenuNavigation = CrossPointSettings::TILT_NORMAL;
  EXPECT_TRUE(screen.acceptsTiltRows());
  screen.modalActive = true;
  EXPECT_FALSE(screen.acceptsTiltRows());
  EXPECT_FALSE(screen.queueTiltRow(true, false));

  const int before = screen.ring();
  tiltfixture::reset();
  tiltfixture::injectPhysicalVertical();
  screen.loop();
  EXPECT_FALSE(tiltfixture::lastVerticalTargetActive);
  EXPECT_EQ(screen.ring(), before);
}

// A value list open over the rows takes the row tilt, as its own up and down buttons would.
TEST_F(TabScreenFixture, NghiengDocDiTrongPopupChonGiaTri) {
  SETTINGS.tiltMenuNavigation = CrossPointSettings::TILT_NORMAL;
  const StrId options[3] = {StrId::STR_STATE_OFF, StrId::STR_STATE_ON, StrId::STR_STATE_OFF};
  screen.popup.show(StrId::STR_STATE_OFF, options, 3, 0, [](int) {});
  const int before = screen.ring();

  tiltfixture::reset();
  tiltfixture::injectPhysicalVertical();
  screen.loop();
  EXPECT_TRUE(tiltfixture::lastVerticalTargetActive) << "the open popup arms the row tilt";
  EXPECT_EQ(screen.popup.selected(), 1) << "Normal steps one option down, as in the list";
  EXPECT_EQ(screen.ring(), before) << "the rows under the popup stay put";

  SETTINGS.tiltMenuNavigation = CrossPointSettings::TILT_NVERTED;
  tiltfixture::reset();
  tiltfixture::injectPhysicalVertical();
  screen.loop();
  EXPECT_EQ(screen.popup.selected(), 0);
}

// Deeper screens are lists without tabs; the row tilt walks them too.
TEST_F(TabScreenFixture, NghiengDocDiDongOManKhongCoThe) {
  SETTINGS.tiltMenuNavigation = CrossPointSettings::TILT_NORMAL;
  FakePlainScreen plain{renderer, input};
  plain.onEnter();
  const int before = plain.selected();
  tiltfixture::reset();
  tiltfixture::injectPhysicalVertical();
  plain.loop();
  EXPECT_TRUE(tiltfixture::lastVerticalTargetActive);
  EXPECT_EQ(plain.selected(), before + 1);
  EXPECT_EQ(tiltfixture::lastMode, CrossPointSettings::TILT_OFF) << "no tab flick on a screen without tabs";
}

TEST_F(TabScreenFixture, ManKhongCoDongThiNghiengDocDungYen) {
  SETTINGS.tiltMenuNavigation = CrossPointSettings::TILT_NORMAL;
  screen.rows = 0;
  screen.loop();
  ASSERT_EQ(screen.ring(), 0);
  EXPECT_FALSE(screen.acceptsTiltRows());

  tiltfixture::reset();
  tiltfixture::injectPhysicalVertical();
  screen.loop();
  EXPECT_EQ(screen.ring(), 0);
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
  EXPECT_EQ(s.order[1], (std::array<uint8_t, 9>{4, 0, 1, 2, 3, 5, 6, 0, 0}));
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

TEST(MenuFavoritesCompatibility, MissingRtcKeepsSystemTimePinsReachable) {
  ASSERT_FALSE(halClock.isAvailable());
  EXPECT_EQ(menufavorites::label("status/statusBarClock", {}), StrId::STR_STATUS_CORNERS);
  EXPECT_EQ(menufavorites::label("action/2", {}), StrId::STR_STATUS_CORNERS);
  EXPECT_EQ(menufavorites::label("clock/clockFormat", {}), StrId::STR_CLOCK_FORMAT);
  EXPECT_EQ(menufavorites::label("action/14", {}), StrId::STR_CLOCK);
}


// Luu cai dat khong duoc doi mot khoi bo nho lien lon. Duong nay chay moi lan
// nguoi dung bat mot tuy chon, doi co chu, gan nut, hay xep lai mot dong ghim.
TEST(SettingsSaveAllocation, SavingDoesNotAskForOneLargeContiguousBlock) {
  JsonDocument warm;
  SETTINGS.toJson(warm);  // lan dau co the dung bang, khong tinh

  JsonDocument doc;
  alloctest::reset();
  alloctest::recording = true;
  SETTINGS.toJson(doc);
  alloctest::recording = false;

  // 8 KB la nguong rong rai: bang cai dat that xin 16.560 byte.
  EXPECT_LT(alloctest::largest, 8u * 1024u)
      << "luu cai dat xin mot khoi " << alloctest::largest
      << " byte lien nhau; dong bo nho phan manh la cu nay truot va may abort()";
}

// Doc cai dat luc khoi dong di qua cung duong do.
TEST(SettingsSaveAllocation, LoadingDoesNotAskForOneLargeContiguousBlock) {
  JsonDocument doc;
  SETTINGS.toJson(doc);

  alloctest::reset();
  alloctest::recording = true;
  SETTINGS.fromJson(doc);
  alloctest::recording = false;

  EXPECT_LT(alloctest::largest, 8u * 1024u)
      << "doc cai dat xin mot khoi " << alloctest::largest << " byte lien nhau";
}
