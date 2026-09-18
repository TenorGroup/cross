#pragma once

#include <vector>

#include "activities/UiListActivity.h"

// UiListActivity variant for screens with a tab band above the list (Settings,
// Text Settings). Navigation keeps position 0 for the tab bar and 1..N for
// list rows, so props.selectedIndex = ring - 1 (-1 = tab band focused). Front
// button release navigation wraps only within rows 1..N; edge buttons step tabs.
// Each tab owns its own ListNav (selection + viewport memory); activeNav()
// redirects the whole UiListActivity protocol (touch routing, swipe scroll,
// screen sync) to the active tab's state. Button navigation walks the row ring
// on release and steps the TAB on continuous hold. The tab-bar chrome (pill
// styles, focused band wash) is shared verbatim via buildTabBar().
//
// Subclasses own the button semantics wholesale (handleButtons is pure here:
// the two existing tab screens differ on press-vs-release and what Back does)
// plus what activating a row or tapping a tab means.
class UiTabListActivity : public UiListActivity {
 public:
  void onEnter() override;
  void captureNavigation(MenuNavigationState& state) const override;
  void restoreNavigation(const MenuNavigationState& state) override;

  struct CuaSoThe {
    int dau = 0;  // chi so the dau tien hien ra
    int dai = 0;  // so the hien ra
  };
  // Ham thuan, khong dung trang thai nao ngoai bon tham so, nen kiem duoc thang.
  //   tong      tong so the
  //   dangChon  the dang chon
  //   dauCu     dau cua so lan ve truoc
  //   vua       so the vua man, do duoc tu be rong nhan
  // Cua so TRUOT TUNG BUOC: chi dich khi the dang chon cham mep, va dich dung mot o.
  static CuaSoThe tinhCuaSo(int tong, int dangChon, int dauCu, int vua);

  // So the vua man, do tu be rong thanh the va nhan RONG NHAT cua ca danh sach.
  //   beRong        be rong thanh the, diem anh
  //   nhanRongNhat  be rong nhan dai nhat trong ca danh sach
  //   vien          phan le moi o an mat (tabInset cong contentInset)
  //   khoang        khoang ho GIUA hai o (TabBarProps::gap)
  //   tong          tong so the
  //
  // Quen `khoang` la ra so sai: do 14/09 tren menu doc, nhan "Yeu thich" rong 93 px va
  // phep tinh bo qua khoang ho noi la nam the vua, trong khi tren man no bi cat.
  // Tran la 5, san la 4. Do nhan rong nhat cua CA danh sach chu khong cua rieng cua
  // so dang hien, de be rong o khong nhay giat moi lan truot.
  static int theVuaMan(int beRong, int nhanRongNhat, int vien, int khoang, int tong);

 protected:
  virtual int preferredTabBarHeight() const;
  // Tab-bar action; subclass actions start at ACTION_TAB_USER.
  static constexpr freeink::ui::ActionId ACTION_TAB = ACTION_USER;
  static constexpr freeink::ui::ActionId ACTION_TAB_USER = ACTION_USER + 1;

  UiTabListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  // --- subclass contract (in addition to UiListActivity's) ------------------
  virtual int tabCount() const = 0;
  virtual int activeTab() const = 0;
  virtual const char* tabLabel(int index) const = 0;
  virtual freeink::ui::BitmapRef tabIcon(int /*index*/) const { return {}; }

  // Touch tap on a tab pill (bounds already checked).
  virtual void onTabAction(int index) = 0;
  // Advance the active tab by direction (continuous-hold navigation; also what
  // Confirm on the tab bar should do). Subclass owns wrap and any per-switch
  // state reset, and requests the update.
  virtual void stepTab(int direction) = 0;
  // Moi man the tu quyet nghia cua Chon va Quay lai; luat chung tu 14/09/2026 dem: Chon o thanh
  // the buoc xuong dong dau, Quay lai roi man mot nhip.
  bool handleButtons() override = 0;

  // Giu cap nut di-dong (hai nut mat truoc) khi con tro dang o tren mot DONG.
  // Tra ve true nghia la lop con da tieu nhip giu do cho viec rieng cua no, va luc do
  // con tro dung yen. Mac dinh khong tieu, nen giu van la lap lai buoc di nhu cu.
  //
  // Cai moc nay ton tai de tab Yeu thich cua menu doc dung duoc nhip giu cho viec xep
  // lai thu tu, ma khong phai chep lai ca khoi gan nut o day.
  virtual bool giuNutDiDong(int /*huong*/) { return false; }

  // Tab stepping runs on its own navigator: ButtonNavigator keeps one
  // hold-suppression latch per instance, and every release clears it even when
  // it suppressed that release. Sharing one navigator across both axes lets a
  // row release swallow, or double-fire, a tab step.
  ButtonNavigator tabNavigator;

  // --- ring plumbing ---------------------------------------------------------
  freeink::ui::ListNav& activeNav() override;
  // Ring position of the active tab (0 = tab bar) for const contexts.
  int ringPos() const;
  int favoriteSelectedRow() override { return ringPos() - 1; }
  // ACTION_ROW lands as ring = row + 1, then activateIndex(row).
  void onRowAction(const freeink::ui::ActionEvent& event) override;
  // Release walks the row ring; continuous hold steps the tab. Both queue an
  // intent, so a press is never lost while the panel draws.
  void navigateButtons() override;
  // Ring walk for the StepNext/StepPrev intents (1..count, wrapping).
  void stepSelection(int direction) override;
  // TabNext/TabPrev: the subclass switch takes the render lock itself, so the
  // applier dispatches these with no lock held.
  void applyTabStep(int direction) override { stepTab(direction); }
  // First row with the viewport pulled to it: ring 1 (Home's continue-reading
  // card), or ring 0 when the list is empty.
  void applyFirstRow() override;
  // UiListActivity hook: clamp the active tab's ring cursor. Runs inside
  // applyPendingNav(), under the render lock, before Confirm reads the
  // selection and after every applied move or tab switch.
  bool clampAfterNav() override;
  bool handleTabHoldNavigation();
  int adjacentTab(int direction) const;
  bool tabChordBlocked = false;
  // Move to a ring position: tab bar rewinds the viewport, a row pulls the
  // viewport to itself. Landing on a row in a tab other than the one that last
  // held the cursor drops every other tab's remembered position (see rowTab).
  void moveRingTo(int ringIndex);
  void commitTabNavigation();

  static constexpr int16_t MUI_TEN_LE = 14;   // mang le moi ben, danh cho mui ten
  static constexpr int16_t MUI_TEN_RONG = 6;  // be ngang mui ten
  static constexpr int16_t MUI_TEN_CAO = 10;  // chieu cao mui ten
  void veMuiTenThe(UiScreen& screen, const freeink::ui::Rect& thanh, int16_t le);

  // The shared tab band: theme-driven pill treatment (label-hugging Lyra vs
  // full-slot RoundedRaff), Lyra focused band wash, always-on divider.
  void buildTabBar(UiScreen& screen);
  // Ring-aware counterpart of syncListViewport: measures rows, applies the
  // one-shot follow to the remembered row, clamps, and writes
  // props.selectedIndex = ring - 1. hasSubtitle: see syncListViewport().
  void syncTabListViewport(UiScreen& screen, freeink::ui::ListProps& props, bool hasSubtitle = false);

  // Cua so the cua lan ve gan nhat. Bai kiem doc hai so nay qua lop con.
  // Chay phep chia cua so va ghi lai ket qua. buildTabBar() goi no voi so the do
  // duoc; bai kiem goi no voi mot con so dat san.
  void capNhatCuaSoThe(int vua);

  int tabWindowStart() const { return cuaSoDau; }
  int tabWindowCount() const { return cuaSoDai; }

  // Per-tab selection/viewport state, sized in onEnter. Protected so subclass
  // tab-switch code can seed the target tab's ring/viewport.
  std::vector<freeink::ui::ListNav> tabNavs;

 private:
  // Cua so the, ghi lai moi lan buildTabBar() chay.
  int cuaSoDau = 0;
  int cuaSoDai = 0;

  // Tab whose rows the cursor last entered, -1 before any row is selected.
  // The two tab-stepping buttons sit on the device's edge under the holding
  // hand, so a stray press is common: stepping away and back must return to the
  // row you were on. Moving the cursor onto a row in a different tab is the
  // deliberate act that ends that grace, and drops the other tabs' positions.
  int rowTab = -1;
  void forgetOtherTabs();

  static void tabActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
};
