#include "UiListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "MenuCustomization.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;
namespace {
struct PinDecoration {
  fui::ListItem* row = nullptr;
  const char* original = nullptr;
  std::string text;
};
std::array<PinDecoration, 32> pinDecorations;
size_t pinDecorationCount = 0;
void restorePinnedRows() {
  for (size_t i = 0; i < pinDecorationCount; ++i) pinDecorations[i].row->label = pinDecorations[i].original;
  pinDecorationCount = 0;
}
}  // namespace

UiListActivity::UiListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const bool wantsTouchLongPress)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer), wantsTouchLongPress(wantsTouchLongPress) {}

void UiListActivity::onEnter() {
  Activity::onEnter();
  activeNav().reset();
  resetUi();
  app.on(ACTION_ROW, &UiListActivity::rowActionTrampoline, this);
  app.setScreen(&UiListActivity::screenTrampoline, this);
  requestUpdate();
}

void UiListActivity::screenTrampoline(UiScreen& screen, void* user) {
  auto* self = static_cast<UiListActivity*>(user);
  self->buildScreen(screen);
}

void UiListActivity::rowActionTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiListActivity*>(user);
  if (event.value < 0 || event.value >= self->listCount()) return;
  self->onRowAction(event);
}

void UiListActivity::onRowAction(const fui::ActionEvent& event) {
  activeNav().selected = event.value;
  if (event.longPress) {
    onRowLongPress(event.value);
    return;
  }
  activateIndex(event.value);
}

bool UiListActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int selected = activeNav().selected;
    if (selected >= 0 && selected < listCount()) activateIndex(selected);
    return true;
  }
  return false;
}

bool UiListActivity::routeListTouch() {
  // Touch goes through the FreeInkApp: render() registered the row hit rects;
  // route the snapshot and let the action trampoline dispatch.
  const auto route = UiAppHost::routeTouch(mappedInput, wantsTouchLongPress);
  // No pressed-state repaint: the render it triggers would drop a slow tap's
  // release inside the uiReady window (tap-to-activate needed two taps), and
  // it costs a second e-ink refresh per tap.
  if (route.routed && app.invalidated()) requestUpdate();
  return static_cast<bool>(route);  // dispatched to the action handler
}

void UiListActivity::moveSelectionTo(const int index) {
  {
    // The render task reads nav mid-build (syncToProps, layout feedback); a
    // press landing during a render would otherwise tear selection/viewport.
    RenderLock lock(*this);
    auto& n = activeNav();
    n.selected = index;
    n.follow(listCount());
  }
  requestUpdate();
}

void UiListActivity::loop() {
  if (!pendingFavorite.empty()) {
    const std::string key = std::move(pendingFavorite);
    pendingFavorite.clear();
    const int row = focusFavorite(key);
    if (row >= 0 && activateFavorite) activateIndex(row);
    requestUpdate();
    return;
  }
  if (handleCustomInput()) return;
  if (supportsFavorites() && !favoriteKey(favoriteSelectedRow()).empty() &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 700)) {
    const std::string key = favoriteKey(favoriteSelectedRow());
    if (!key.empty()) {
      RenderLock lock(*this);
      favoriteSaveFailed = !toggleFavorite(favoriteSelectedRow());
      favoritesChanged();
    }
    requestUpdate();
    return;
  }
  if (handleButtons()) return;
  if (routeListTouch()) return;

  // Swipes scroll the viewport; the selection stays put (it may scroll
  // off-screen) and button navigation pulls the view back to it.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    bool moved = false;
    {
      // Same nav-vs-render race as moveSelectionTo: the render task writes
      // pageRows/top mid-build, so read and mutate under one lock.
      RenderLock lock(*this);
      auto& n = activeNav();
      const int delta = swipe == MappedInputManager::SwipeDir::Up ? n.pageRows() : -n.pageRows();
      moved = n.scrollBy(delta, listCount());
    }
    if (moved) requestUpdate();
    return;
  }

  navigateButtons();
}

void UiListActivity::moveListPage(const int direction) {
  bool moved = false;
  {
    RenderLock lock(*this);
    auto& n = activeNav();
    const int count = listCount();
    const int rows = std::max(1, n.pageRowsFor(count));
    moved = n.scrollBy(direction * rows, count);
    if (moved) {
      // Start at the first displayed item. Following the old selection here
      // would pull the viewport back and turn Page Down into a one-row scroll.
      n.selected = n.top;
      n.followOnBuild = false;
      n.followPending = false;
    }
  }
  if (moved) requestUpdate();
}

void UiListActivity::moveToVisibleBoundary(const bool last, const bool ring) {
  {
    RenderLock lock(*this);
    auto& n = activeNav();
    const int count = listCount();
    const int first = kepConTro(n.top, count);
    const int end = kepConTro(first + std::max(1, n.pageRowsFor(count)) - 1, count);
    n.selected = count > 0 ? (last ? end : first) + (ring ? 1 : 0) : 0;
    n.followOnBuild = false;
    n.followPending = false;
  }
  requestUpdate();
}

void UiListActivity::navigateButtons() {
  using Button = MappedInputManager::Button;
  constexpr unsigned long HOLD_MS = 700;
  const int count = listCount();
  // Physical front Up/Down are logical Left/Right on X3. Its edge pair are
  // logical Up/Down. wasLongPressed fires once and swallows the release.
  if (mappedInput.wasLongPressed(Button::Left, HOLD_MS) || mappedInput.wasLongPressed(Button::Up, HOLD_MS)) {
    moveToVisibleBoundary(false);
    return;
  }
  if (mappedInput.wasLongPressed(Button::Right, HOLD_MS) || mappedInput.wasLongPressed(Button::Down, HOLD_MS)) {
    moveToVisibleBoundary(true);
    return;
  }
  if (mappedInput.wasReleased(Button::Right)) {
    moveSelectionTo(ButtonNavigator::nextIndex(activeNav().selected, count));
  } else if (mappedInput.wasReleased(Button::Left)) {
    moveSelectionTo(ButtonNavigator::previousIndex(activeNav().selected, count));
  } else if (mappedInput.wasReleased(Button::Down)) {
    if (!activityManager.switchSettingsSibling(1)) moveListPage(1);
  } else if (mappedInput.wasReleased(Button::Up)) {
    if (!activityManager.switchSettingsSibling(-1)) moveListPage(-1);
  }
}

void UiListActivity::syncListViewport(UiScreen& screen, fui::ListProps& props, const bool hasSubtitle) {
  reserveFixedMenuContent(screen);
  reserveFavoriteHint(screen);
  decoratePinnedRows(props);
  int16_t rowHeight = screen.theme().rowHeight;
  if (!mappedInput.hasTouch()) {
    // Non-touch hardware (X3/X4) keeps the original, denser per-theme row
    // height instead of FreeInkUI's touch-target-sized default, so lists fit
    // as many rows per screen as they did before the FreeInkUI migration.
    // props.rowHeight must be set explicitly: screen.list() otherwise falls
    // back to the (touch-friendly) theme token, not this local value.
    // A label that must wrap (labelText.maxLines > 1) grows only its own row:
    // list() sizes wrapped items per-row, so the dense height stays.
    const auto& metrics = UITheme::getInstance().getMetrics();
    rowHeight = static_cast<int16_t>(hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight);
    props.rowHeight = rowHeight;
  }
  // Reserve a small hint band before measuring the list, so arrows cannot
  // cover the final item. Tabbed screens use their own tab arrows instead.

  activeNav().syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, listCount(), props);

  activeNav().selected = kepConTro(activeNav().selected, listCount());
  props.selectedIndex = static_cast<int16_t>(activeNav().selected);
}

void UiListActivity::renderUi() {
  tabBandDrawn = false;
  favoriteHintY = -1;
  UiAppHost::renderUi();
  restorePinnedRows();
  if (tenorchrome::enabled() && !tabBandDrawn) {
    const int count = listCount();
    const auto& n = activeNav();
    const int first = count > 0 ? std::min(n.top + 1, count) : 0;
    const int last = std::min(count, n.top + n.pageRows());
    char label[48];
    snprintf(label, sizeof(label), "%d-%d / %d", first, last, count);
    const int w = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - 18 - w, tenorchrome::TAB_TOP + 18, label);
  }
  drawPageHints();
  if (favoriteHintY >= 0) {
    const std::string key = favoriteKey(favoriteSelectedRow());
    const StrId hint = favoriteSaveFailed                           ? StrId::STR_MENU_SAVE_FAILED
                       : menucustom::state().find(key.c_str()) >= 0 ? StrId::STR_MENU_PINNED
                                                                    : StrId::STR_MENU_PIN_HINT;
    if (!key.empty() || favoriteSaveFailed) tenorchrome::drawTip(renderer, I18N.get(hint));
  }
}

void UiListActivity::drawPageHints() {
  if (mappedInput.hasTouch() || SETTINGS.uiTheme != CrossPointSettings::TENOR_UI || !SETTINGS.tenorSideArrows) return;
  constexpr int cy = 195;
  const int right = renderer.getScreenWidth() - 4;
  // Paint after the list: drawing during screen construction is covered by
  // the list background on full-width menus such as Settings.
  for (int dx = 0; dx <= 6; ++dx) {
    const int halfHeight = dx * 4 / 6;
    renderer.drawLine(4 + dx, cy - halfHeight, 4 + dx, cy + halfHeight, true);
    renderer.drawLine(right - dx, cy - halfHeight, right - dx, cy + halfHeight, true);
  }
}

int UiListActivity::kepConTro(const int chon, const int soDong) {
  if (soDong <= 0) return 0;
  if (chon < 0) return 0;
  return chon < soDong ? chon : soDong - 1;
}

void UiListActivity::drawChrome() {
  const char* title = headerTitle();
  if (title) drawNavigationHeader(title);
}

void UiListActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void UiListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  // Wrapped labels grow rows, so fewer rows can fit than the fixed-height
  // estimate ListNav plans with. list() reports the real layout back
  // (ListNav::onListRendered); when the selection landed past the drawn rows
  // the nav advanced the viewport and asked for another build. Bounded: top
  // strictly advances toward the selection each pass.
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  drawFooter();
  renderer.displayBuffer();
}

void UiListActivity::captureNavigation(MenuNavigationState& state) const {
  state.count = 1;
  state.cursors[0] = {nav.selected, nav.top};
}
void UiListActivity::restoreNavigation(const MenuNavigationState& state) {
  if (state.count != 1) return;
  RenderLock lock(*this);
  nav.selected = kepConTro(state.cursors[0].selected, listCount());
  nav.top = state.cursors[0].top;
  nav.followOnBuild = true;
}

void UiListActivity::reserveFixedMenuContent(UiScreen& screen) {
  if (tenorchrome::enabled() && screen.body().y < tenorchrome::CONTENT_TOP)
    screen.takeTop(static_cast<int16_t>(tenorchrome::CONTENT_TOP - screen.body().y));
}

int UiListActivity::focusFavorite(const std::string& key) {
  for (int i = 0; i < listCount(); ++i) {
    if (favoriteKey(i) != key) continue;
    RenderLock lock(*this);
    activeNav().selected = i;
    activeNav().followOnBuild = true;
    return i;
  }
  return -1;
}
void UiListActivity::reserveFavoriteHint(UiScreen& screen) {
  if (!supportsFavorites()) return;
  favoriteHintY = tenorchrome::tipY(renderer);
  const int bottom = screen.body().y + screen.body().height;
  const int reservedTop = favoriteHintY - 2;
  if (bottom > reservedTop) screen.takeBottom(static_cast<int16_t>(bottom - reservedTop));
}

bool UiListActivity::toggleFavorite(int row) {
  const auto key = favoriteKey(row);
  return menucustom::togglePin(key.c_str());
}
bool UiListActivity::rowIsPinned(int row) const {
  const auto key = favoriteKey(row);
  return !key.empty() && menucustom::state().find(key.c_str()) >= 0;
}
void UiListActivity::decoratePinnedRows(fui::ListProps& props) {
  if (!props.items) return;
  const int end = props.itemsWindowCount ? props.itemsWindowFirst + props.itemsWindowCount : props.count;
  for (int i = props.itemsWindowFirst; i < end && pinDecorationCount < pinDecorations.size(); ++i) {
    if (!rowIsPinned(i)) continue;
    auto& d = pinDecorations[pinDecorationCount++];
    d.row = const_cast<fui::ListItem*>(&props.items[i - props.itemsWindowFirst]);
    d.original = d.row->label;
    d.text = "\xEE\x84\x8A";
    d.text += d.original ? d.original : "";
    d.row->label = d.text.c_str();
  }
}
