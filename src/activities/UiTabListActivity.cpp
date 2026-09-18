#include "UiTabListActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cassert>

#include "MappedInputManager.h"
#include "MenuCustomization.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr int16_t TOUCH_TAB_BAR_HEIGHT = 50;
}

UiTabListActivity::UiTabListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity(name, renderer, mappedInput) {}

void UiTabListActivity::onEnter() {
  // Size the per-tab state before the base resets activeNav() (which indexes
  // into it).
  menucustom::load();
  tabNavs.assign(static_cast<size_t>(tabCount()), fui::ListNav{});
  rowTab = -1;
  UiListActivity::onEnter();
  if (!mappedInput.hasTouch()) {
    for (auto& cursor : tabNavs) cursor.selected = 1;
  }
  app.on(ACTION_TAB, &UiTabListActivity::tabActionTrampoline, this);
}

bool UiTabListActivity::clampAfterNav() {
  auto& cursor = activeNav();
  const int count = listCount();
  const int selected = count <= 0 ? 0 : std::clamp(cursor.selected, mappedInput.hasTouch() ? 0 : 1, count);
  if (selected == cursor.selected) return false;
  cursor.selected = selected;
  cursor.followOnBuild = true;
  return true;
}

fui::ListNav& UiTabListActivity::activeNav() {
  if (tabNavs.empty()) return nav;  // pre-onEnter fallback
  // Invariant: subclasses keep activeTab() inside [0, tabCount()), and
  // tabCount() does not change after onEnter() sized tabNavs.
  assert(activeTab() >= 0 && static_cast<size_t>(activeTab()) < tabNavs.size());
  return tabNavs[static_cast<size_t>(activeTab())];
}

int UiTabListActivity::ringPos() const {
  if (tabNavs.empty()) return 0;
  assert(activeTab() >= 0 && static_cast<size_t>(activeTab()) < tabNavs.size());
  return tabNavs[static_cast<size_t>(activeTab())].selected;
}

void UiTabListActivity::tabActionTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiTabListActivity*>(user);
  if (event.value < 0 || event.value >= self->tabCount()) return;
  self->onTabAction(event.value);
  if (self->clampAfterNav()) self->requestUpdate();
}

void UiTabListActivity::onRowAction(const fui::ActionEvent& event) {
  {
    RenderLock lock(*this);
    moveRingTo(event.value + 1);
  }
  activateIndex(event.value);
}

void UiTabListActivity::forgetOtherTabs() {
  for (size_t i = 0; i < tabNavs.size(); i++) {
    if (static_cast<int>(i) == activeTab()) continue;
    tabNavs[i].selected = mappedInput.hasTouch() ? 0 : 1;
    tabNavs[i].top = 0;
  }
}

void UiTabListActivity::moveRingTo(const int ringIndex) {
  auto& n = activeNav();
  n.selected = ringIndex;
  if (ringIndex == 0) {
    n.top = 0;
  } else {
    // Pull the viewport to the row (ring - 1); ListNav::follow reads
    // n.selected as a row index, so compute directly here.
    const uint16_t rows = n.visibleRows > 0 ? static_cast<uint16_t>(n.visibleRows) : 1;
    n.top = fui::listTopIndexFor(static_cast<int16_t>(ringIndex - 1), static_cast<uint16_t>(n.top < 0 ? 0 : n.top),
                                 rows, static_cast<uint16_t>(listCount()));
  }
  requestUpdate();
}

bool UiTabListActivity::handleTabHoldNavigation() {
  using Button = MappedInputManager::Button;
  constexpr unsigned long HOLD_MS = 700;
  const bool up = mappedInput.isPressed(Button::Up), down = mappedInput.isPressed(Button::Down);
  if (up && down) {
    tabChordBlocked = true;
    mappedInput.wasLongPressed(Button::Up, 0);
    mappedInput.wasLongPressed(Button::Down, 0);
  }
  if (tabChordBlocked) {
    if (!up && !down) tabChordBlocked = false;
    return true;
  }
  for (int direction : {-1, 1}) {
    const Button button = direction < 0 ? Button::Up : Button::Down;
    if (mappedInput.wasLongPressed(button, HOLD_MS)) {
      {
        RenderLock lock(*this);
        menucustom::moveTab(menucustom::groupFor(name.c_str()), activeTab(), tabCount(), direction);
      }
      requestUpdate();
      return true;
    }
  }
  // Favorites keeps its explicitly approved hold-to-reorder gesture.
  if (mappedInput.isPressed(Button::Left) && giuNutDiDong(-1)) return true;
  if (mappedInput.isPressed(Button::Right) && giuNutDiDong(1)) return true;
  if (mappedInput.wasLongPressed(Button::Left, HOLD_MS)) {
    moveToVisibleBoundary(false, true);
    return true;
  }
  if (mappedInput.wasLongPressed(Button::Right, HOLD_MS)) {
    moveToVisibleBoundary(true, true);
    return true;
  }
  return false;
}

void UiTabListActivity::navigateButtons() {
  if (handleTabHoldNavigation()) return;
  // Two independent axes, split by where the button physically sits on the X3.
  // The front pair (logical Left/Right, labelled up/down on screen) walks rows
  // 1..N and never enters the tab-band slot 0; the two edge buttons (logical
  // Up/Down) step the tab, so a tab is always one press away from any row. Note
  // the logical names run opposite to the physical positions on this board.
  //
  // Each axis binds explicit buttons rather than NavNext/NavPrevious, which fold
  // the edge button into the front one, and each runs on its own navigator:
  // ButtonNavigator keeps one hold-suppression latch per instance and clears it
  // on every release, so one shared navigator lets the axes corrupt each other.
  buttonNavigator.onRelease({MappedInputManager::Button::Right}, [this] { queueNavIntent(NavIntent::StepNext); });
  buttonNavigator.onRelease({MappedInputManager::Button::Left}, [this] { queueNavIntent(NavIntent::StepPrev); });
  tabNavigator.onRelease({MappedInputManager::Button::Down}, [this] { queueNavIntent(NavIntent::TabNext); });
  tabNavigator.onRelease({MappedInputManager::Button::Up}, [this] { queueNavIntent(NavIntent::TabPrev); });
}

void UiTabListActivity::stepSelection(const int direction) {
  const int count = listCount();
  if (count <= 0) {
    moveRingTo(0);
    return;
  }
  const int ring = ringPos();
  if (direction > 0) {
    moveRingTo(ring <= 0 || ring >= count ? 1 : ring + 1);
  } else {
    moveRingTo(ring <= 1 || ring > count ? count : ring - 1);
  }
}

void UiTabListActivity::applyFirstRow() { moveRingTo(listCount() > 0 ? 1 : 0); }

void UiTabListActivity::syncTabListViewport(UiScreen& screen, fui::ListProps& props, const bool hasSubtitle) {
  reserveFixedMenuContent(screen);
  reserveFavoriteHint(screen);
  decoratePinnedRows(props);
  const int count = listCount();
  clampAfterNav();
  auto& n = activeNav();
  int16_t rowHeight = screen.theme().rowHeight;
  if (!mappedInput.hasTouch()) {
    // Non-touch hardware (X3/X4) keeps the original, denser per-theme row
    // height instead of FreeInkUI's touch-target-sized default (see
    // UiListActivity::syncListViewport, the non-tab counterpart of this).
    const auto& metrics = UITheme::getInstance().getMetrics();
    rowHeight = static_cast<int16_t>(hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight);
    // Wrapped (maxLines > 1) labels grow only their own row: list() sizes
    // wrapped items per-row, so the dense height stays for the rest.
    props.rowHeight = rowHeight;
  }
  const uint16_t rows =
      fui::listVisibleRows(screen.body(), rowHeight, props.rowGap >= 0 ? props.rowGap : screen.theme().listRowGap);
  n.visibleRows = rows > 0 ? rows : 1;
  if (n.followOnBuild) {
    // Screen entry / tab switch: show the tab's remembered selection, or the
    // top when the tab bar holds the focus.
    n.followOnBuild = false;
    n.top = n.selected > 0 ? static_cast<int>(fui::listTopIndexFor(
                                 static_cast<int16_t>(n.selected - 1), static_cast<uint16_t>(n.top < 0 ? 0 : n.top),
                                 static_cast<uint16_t>(n.visibleRows), static_cast<uint16_t>(count)))
                           : 0;
  }
  n.scrollBy(0, count);  // clamp to range
  props.topIndex = static_cast<uint16_t>(n.top);
  props.selectedIndex = static_cast<int16_t>(n.selected - 1);  // -1 = tab band focused
}

UiTabListActivity::CuaSoThe UiTabListActivity::tinhCuaSo(const int tong, const int dangChon, const int dauCu,
                                                         const int vua) {
  CuaSoThe ra;
  if (tong <= 0) return ra;

  int dai = vua < 1 ? 1 : vua;
  if (dai > tong) dai = tong;

  // Keo dau cua so ve trong bo, roi moi keo the dang chon vao tam nhin. Hai buoc
  // tach roi vi dauCu co the la rac: so the doi giua hai lan ve.
  int dau = dauCu < 0 ? 0 : dauCu;
  if (dau > tong - dai) dau = tong - dai;

  // TRUOT TUNG BUOC: chi dich khi the dang chon rot ra ngoai, va dich vua du de
  // no lot vao lai. Khong nhay nguyen trang, vi nhay trang lam the dang chon vot
  // tu mep nay sang mep kia va mat phai di tim lai.
  if (dangChon < dau) {
    dau = dangChon;
  } else if (dangChon >= dau + dai) {
    dau = dangChon - dai + 1;
  }

  if (dau < 0) dau = 0;
  if (dau > tong - dai) dau = tong - dai;

  ra.dau = dau;
  ra.dai = dai;
  return ra;
}

int UiTabListActivity::theVuaMan(const int beRong, const int nhanRongNhat, const int vien, const int khoang,
                                 const int tong) {
  constexpr int TRAN = 5;
  constexpr int SAN = 4;
  if (tong <= 0) return 0;

  const int tran = TRAN < tong ? TRAN : tong;
  const int san = SAN < tong ? SAN : tong;
  for (int n = tran; n > san; n--) {
    // Be rong mot o: ca thanh tru het khoang ho giua cac o, roi chia deu.
    const int o = (beRong - khoang * (n - 1)) / n;
    if (o >= nhanRongNhat + vien) return n;
  }
  return san;
}

void UiTabListActivity::capNhatCuaSoThe(const int vua) {
  const int position = menucustom::position(menucustom::groupFor(name.c_str()), activeTab(), tabCount());
  const CuaSoThe cuaSo = tinhCuaSo(tabCount(), position, cuaSoDau, vua);
  cuaSoDau = cuaSo.dau;
  cuaSoDai = cuaSo.dai;
}

void UiTabListActivity::veMuiTenThe(UiScreen& screen, const fui::Rect& thanh, const int16_t le) {
  // Mui ten DAC, be, nam giua chieu cao bang the. Chi hien ben nao that su con the an.
  const int16_t giua = static_cast<int16_t>(thanh.y + thanh.height / 2);
  const int16_t nua = static_cast<int16_t>(MUI_TEN_CAO / 2);
  const auto muc = fui::Paint::solid(fui::Color::Black);

  if (cuaSoDau > 0) {
    const int16_t x = static_cast<int16_t>(thanh.x + (le - MUI_TEN_RONG) / 2);
    screen.target().triangle(fui::Point{static_cast<int16_t>(x + MUI_TEN_RONG), static_cast<int16_t>(giua - nua)},
                             fui::Point{static_cast<int16_t>(x + MUI_TEN_RONG), static_cast<int16_t>(giua + nua)},
                             fui::Point{x, giua}, muc);
  }
  if (cuaSoDau + cuaSoDai < tabCount()) {
    const int16_t x = static_cast<int16_t>(thanh.right() - le + (le - MUI_TEN_RONG) / 2);
    screen.target().triangle(fui::Point{x, static_cast<int16_t>(giua - nua)},
                             fui::Point{x, static_cast<int16_t>(giua + nua)},
                             fui::Point{static_cast<int16_t>(x + MUI_TEN_RONG), giua}, muc);
  }
}

int UiTabListActivity::preferredTabBarHeight() const {
  if (tenorchrome::enabled()) return tenorchrome::TAB_HEIGHT;
  return mappedInput.hasTouch() ? TOUCH_TAB_BAR_HEIGHT : UITheme::getInstance().getMetrics().tabBarHeight;
}

void UiTabListActivity::buildTabBar(UiScreen& screen) {
  tabBandDrawn = true;
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Tabs. The selected pill dims to a dither when the selection is down in
  // the list (the legacy focused/unfocused tab distinction).
  // Stack array, not a heap vector: this runs on every render and the tab
  // count is small and fixed.
  constexpr int MAX_TABS = 5;
  fui::TabBarProps tabProps;
  tabProps.action = ACTION_TAB;
  tabProps.iconSize = SETTINGS.uiTheme == CrossPointSettings::TENOR_UI ? 32 : 0;
  tabProps.inputMask = fui::InputTouch;
  // Pill shape and label size are theme-driven. Lyra uses equal-width slots
  // with small labels so wide text (e.g. "Controls") still fits at large UI scales.
  // Full-slot (RoundedRaff): the pill fills its slot like the legacy layout
  // (slot minus a 4px frame, 8px clearance above the divider) with
  // body-size labels; zero horizontal contentInset disables the tabBar's
  // label-width shrink.
  const bool tabsFocused = ringPos() == 0;
  const bool tenorHome = tenorchrome::enabled();
  if (metrics.tabPillFullSlot) {
    tabProps.text = screen.theme().bodyText;
    tabProps.tabInset = fui::Insets{4, 4, 7, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  } else {
    tabProps.text = screen.theme().smallText;
    tabProps.gap = static_cast<int16_t>(metrics.tabSpacing);
    // Unfocused state: no bottom inset, so the pill (and the 2px selected
    // underline drawn along its bottom edge) reaches the band's 1px divider -
    // legacy Lyra drew the underline sitting on that rule, not floating above.
    tabProps.tabInset = tabsFocused ? fui::Insets{2, 4, 4, 4} : fui::Insets{2, 4, 0, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  }
  if (tenorHome) tabProps.tabInset = fui::Insets{2, 4, 4, 4};
  int16_t nhanRongNhat = 0;
  for (int i = 0; i < tabCount(); i++) {
    // The ve bang bieu tuong thi be rong la be rong anh, khong phai be rong chu.
    const fui::BitmapRef anh = tabIcon(i);
    if (anh) {
      if (static_cast<int16_t>(anh.width) > nhanRongNhat) nhanRongNhat = static_cast<int16_t>(anh.width);
      continue;
    }
    const char* nhan = tabLabel(i);
    if (nhan == nullptr || nhan[0] == '\0') continue;
    const int16_t rong = screen.target().measureText(tabProps.text.font, nhan, tabProps.text).width;
    if (rong > nhanRongNhat) nhanRongNhat = rong;
  }
  const int vien =
      tabProps.tabInset.left + tabProps.tabInset.right + tabProps.contentInset.left + tabProps.contentInset.right;
  const int16_t beRongThanh = screen.frame().screen().width;
  const int vuaThu = theVuaMan(beRongThanh, nhanRongNhat, vien, tabProps.gap, tabCount());
  const bool thanhTheChay = vuaThu < tabCount();
  const int16_t leMuiTen = thanhTheChay ? MUI_TEN_LE : 0;
  capNhatCuaSoThe(theVuaMan(beRongThanh - 2 * leMuiTen, nhanRongNhat, vien, tabProps.gap, tabCount()));

  const int count = cuaSoDai < MAX_TABS ? cuaSoDai : MAX_TABS;
  fui::TabItem tabs[MAX_TABS];
  for (int i = 0; i < count; i++) {
    // Chi so THAT trong danh sach the. Phai giu dung, vi `value` la thu cham tay gui
    // nguoc ve onTabAction(): gui chi so trong cua so la cham nham the.
    const int that = menucustom::idAt(menucustom::groupFor(name.c_str()), cuaSoDau + i, tabCount());
    const fui::BitmapRef anh = tabIcon(that);
    if (anh) {
      // Bieu tuong THAY cho chu, khong phai dung canh chu: de ca hai thi o phai chua
      // duoc ca anh lan chu, va so the vua man tut xuong.
      tabs[i].icon = anh;
    } else {
      tabs[i].label = tabLabel(that);
    }
    tabs[i].value = static_cast<int16_t>(that);
    tabs[i].selected = activeTab() == that;
  }
  tabProps.tabs = tabs;
  tabProps.count = static_cast<uint16_t>(count);

  const int16_t tabLineHeight = screen.target().lineHeight(tabProps.text.font);
  const int16_t preferredTabHeight = static_cast<int16_t>(preferredTabBarHeight());
  const int16_t tabBand = preferredTabHeight > tabLineHeight + 10 ? preferredTabHeight : tabLineHeight + 10;
  // Legacy Lyra two-state treatment: with the selection on the tab band, the
  // band fills gray and the active tab is a solid pill; with the selection
  // down in the list, the band is plain and the active tab keeps a gray box
  // with an underline. The 1px rule under the band is always there.
  tabProps.divider = !tenorHome;
  fui::StyleSet tabStyles;
  tabStyles.explicitlySet = true;
  tabStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  if (tenorHome) {
    tabStyles.selected.background = fui::Paint::dither(fui::Color::LightGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::Black);
    tabStyles.selected.radius = screen.theme().listRowRadius;
    tabProps.selectedUnderline = 2;
  } else if (tabsFocused) {
    tabStyles.selected.background = fui::Paint::solid(fui::Color::Black);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = screen.theme().listRowRadius;
  } else if (metrics.tabPillFullSlot) {
    // Legacy RoundedRaff unfocused treatment: same pill, dimmed to dark gray,
    // text stays inverted; no underline.
    tabStyles.selected.background = fui::Paint::dither(fui::Color::DarkGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = screen.theme().listRowRadius;
  } else {
    tabStyles.selected.background = fui::Paint::dither(fui::Color::LightGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::Black);
    tabProps.selectedUnderline = 2;
  }
  // Focus/flash states keep the pill instead of falling back to an unset
  // (blank) style.
  tabStyles.focused = tabStyles.selected;
  tabStyles.active = tabStyles.selected;
  tabProps.tabStyles = tabStyles;
  const fui::Rect contentTabRect = screen.takeTop(tabBand);
  const fui::Rect frameRect = screen.frame().screen();
  // Tab chrome is a full-width screen band like the legacy GUI tab bar. The
  // remaining list content still stays inside the device safe area.
  const fui::Rect tabRect{frameRect.x, contentTabRect.y, frameRect.width, contentTabRect.height};
  // O the nam trong phan giua; hai mang le hai ben danh cho mui ten.
  const fui::Rect theRect{static_cast<int16_t>(tabRect.x + leMuiTen), tabRect.y,
                          static_cast<int16_t>(tabRect.width - 2 * leMuiTen), tabRect.height};
  // Focused band wash is the Lyra treatment; legacy RoundedRaff keeps the
  // band plain in both states.
  if (tabsFocused && !metrics.tabPillFullSlot && !tenorHome) {
    screen.target().fill(tabRect, fui::Paint::dither(fui::Color::LightGray));
  }
  fui::tabBar(screen.frame(), theRect, tabProps);

  if (thanhTheChay) {
    veMuiTenThe(screen, tabRect, leMuiTen);
  }
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
}

void UiTabListActivity::commitTabNavigation() {
  if (activeTab() != rowTab) {
    forgetOtherTabs();
    rowTab = activeTab();
  }
}
void UiTabListActivity::captureNavigation(MenuNavigationState& state) const {
  if (tabNavs.size() > state.cursors.size()) return;
  state.count = static_cast<int>(tabNavs.size());
  state.tab = activeTab();
  state.committedTab = rowTab;
  for (size_t i = 0; i < tabNavs.size(); ++i) state.cursors[i] = {tabNavs[i].selected, tabNavs[i].top};
}
void UiTabListActivity::restoreNavigation(const MenuNavigationState& state) {
  if (state.count != tabCount() || state.tab < 0 || state.tab >= tabCount()) return;
  onTabAction(state.tab);
  RenderLock lock(*this);
  for (size_t i = 0; i < tabNavs.size(); ++i) {
    tabNavs[i].selected = state.cursors[i].selected;
    tabNavs[i].top = state.cursors[i].top;
    tabNavs[i].followOnBuild = true;
  }
  rowTab = state.committedTab;
  clampAfterNav();
}

int UiTabListActivity::adjacentTab(const int direction) const {
  return menucustom::adjacent(menucustom::groupFor(name.c_str()), activeTab(), tabCount(), direction);
}
