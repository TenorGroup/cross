#include "UiListActivity.h"

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "MenuCustomization.h"
#include "components/OptionPopup.h"
#include "components/TenorMenuChrome.h"
#include "components/SettledListRender.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/icons/tenorRowMarks.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyChrome.h"
#include "shells/ugly/UglyInk.h"

namespace fui = freeink::ui;
namespace {
struct PinDecoration {
  fui::ListItem* row = nullptr;
  const char* original = nullptr;
  std::string text;
};
// One per visible row; the X3 lists fit twelve rows at the dense height.
std::array<PinDecoration, 12> pinDecorations;
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
  // Touch: no gray flash on the row that was tapped or held.
  if (tenorchrome::kTouchShell) self->app.clearTapFlash();
  self->onRowAction(event);
}

void UiListActivity::showRowMenu(const StrId* labels, const int count, std::function<void(int)> onSelect,
                                 const fui::ActionId anchorAction, const int anchorValue) {
  rowMenu.showAnchored(app.publishedRect(anchorAction, static_cast<int16_t>(anchorValue)), labels, count,
                       std::move(onSelect));
  requestUpdate();
}

void UiListActivity::onRowLongPress(const int index) {
  if (!tenorchrome::kTouchShell || !supportsFavorites() || favoriteKey(index).empty()) return;
  const StrId label = rowIsPinned(index) ? StrId::STR_UNPIN_FAVORITE : StrId::STR_PIN_FAVORITE;
  showRowMenu(&label, 1, [this, index](int) { queuePinToggle(index); }, ACTION_ROW, index);
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
  if (backReleased()) {
    onBackButton();
    return true;
  }
  if (confirmReleased()) {
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

// A tap on a faded end of a framed list: the row there is a glimpse of the page before or after, so the tap
// turns to that page (a flick's step) and opens nothing.
bool UiListActivity::routeFadedTap() {
  if (!tenorchrome::kTouchShell) return false;
  int x = 0, y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return false;
  const bool top = y >= fadeTopFrom && y < fadeTopTo;
  const bool foot = y >= fadeFootFrom && y < fadeFootTo;
  if (!top && !foot) return false;
  auto& n = activeNav();
  const int count = listCount();
  const int rows = std::max(1, n.pageRowsFor(count));
  const int step = rows > fadeKeepRows() ? rows - fadeKeepRows() : rows;
  n.requestScroll(foot ? step : -step);
  requestUpdate();
  return true;
}

void UiListActivity::queueNavIntent(const NavIntent intent) {
  if (navQueueCount >= NAV_QUEUE_SIZE) {
    LOG_ERR("UI", "Navigation queue full, dropped intent %u", static_cast<unsigned>(intent));
    return;
  }
  navQueue[(navQueueHead + navQueueCount) % NAV_QUEUE_SIZE] = intent;
  ++navQueueCount;
}

UiListActivity::NavIntent UiListActivity::popNavIntent() {
  const NavIntent intent = navQueue[navQueueHead];
  navQueueHead = static_cast<uint8_t>((navQueueHead + 1) % NAV_QUEUE_SIZE);
  --navQueueCount;
  return intent;
}

bool UiListActivity::applyPendingNav() {
  bool changed = false;
  for (;;) {
    if (navQueueCount == 0) {
      // Clamp once per pass: the row set can change under the cursor (a tab
      // switch, a child screen returning) between two passes.
      RenderLock lock(RenderLock::TryTake{});
      if (!lock.acquired()) return false;
      changed |= clampAfterNav();
      if (changed) requestUpdate();
      return true;
    }
    if (navQueue[navQueueHead] == NavIntent::TabNext || navQueue[navQueueHead] == NavIntent::TabPrev) {
      // A tab switch rebuilds the screen's data model and takes the render lock
      // itself (stepTab -> selectTab/selectCategory), so it runs with our scope
      // released. Only from a pass that owns a free panel: waiting here is what
      // swallowed the edge-button presses.
      {
        RenderLock lock(RenderLock::TryTake{});
        if (!lock.acquired()) {
          if (changed) requestUpdate();
          return false;
        }
      }
      const int direction = navQueue[navQueueHead] == NavIntent::TabNext ? 1 : -1;
      popNavIntent();
      applyTabStep(direction);
      changed = true;
      continue;
    }
    {
      RenderLock lock(RenderLock::TryTake{});
      if (!lock.acquired()) {
        if (changed) requestUpdate();
        return false;
      }
      changed |= applyNavIntent(popNavIntent());
      changed |= clampAfterNav();
    }
  }
}

bool UiListActivity::applyNavIntent(const NavIntent intent) {
  switch (intent) {
    case NavIntent::StepNext:
      stepSelection(1);
      return true;
    case NavIntent::StepPrev:
      stepSelection(-1);
      return true;
    case NavIntent::PageNext:
      return applyPage(1);
    case NavIntent::PagePrev:
      return applyPage(-1);
    case NavIntent::BoundaryFirst:
      return applyBoundary(false, false);
    case NavIntent::BoundaryLast:
      return applyBoundary(true, false);
    case NavIntent::BoundaryFirstRing:
      return applyBoundary(false, true);
    case NavIntent::BoundaryLastRing:
      return applyBoundary(true, true);
    case NavIntent::FirstRow:
      applyFirstRow();
      return true;
    case NavIntent::TabNext:
    case NavIntent::TabPrev:
      break;  // dispatched by applyPendingNav(), which owns their lock handling
  }
  return false;
}

void UiListActivity::stepSelection(const int direction) {
  auto& n = activeNav();
  const int count = listCount();
  n.selected =
      direction > 0 ? ButtonNavigator::nextIndex(n.selected, count) : ButtonNavigator::previousIndex(n.selected, count);
  // Whole-page flips, not a row-by-row crawl: one press is still one repaint, and the rows a page
  // really drew (pageRowsFor) decide where the selection leaves it. followPending stays armed so
  // onListRendered() can correct a page whose wrapped rows fit fewer than estimated.
  n.top = ButtonNavigator::pageTopAfterStep(n.selected, n.top, n.pageRowsFor(count), count, direction);
  n.followPending = true;
}

bool UiListActivity::applyPage(const int direction) {
  auto& n = activeNav();
  const int count = listCount();
  const int rows = std::max(1, n.pageRowsFor(count));
  const bool moved = n.scrollBy(direction * rows, count);
  if (moved) {
    // Start at the first displayed item. Following the old selection here
    // would pull the viewport back and turn Page Down into a one-row scroll.
    n.selected = n.top;
    n.followOnBuild = false;
    n.followPending = false;
  }
  return moved;
}

bool UiListActivity::applyBoundary(const bool last, const bool ring) {
  auto& n = activeNav();
  const int count = listCount();
  const int first = kepConTro(n.top, count);
  const int end = kepConTro(first + std::max(1, n.pageRowsFor(count)) - 1, count);
  n.selected = count > 0 ? (last ? end : first) + (ring ? 1 : 0) : 0;
  n.followOnBuild = false;
  n.followPending = false;
  return true;
}

bool UiListActivity::confirmReleased() {
  // Held back only while moves are still queued: the row Select should act on
  // is the one on screen. With an empty queue the selection cannot be stale, so
  // Select fires immediately even if the panel is mid-refresh.
  if (navQueueCount > 0) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) pendingConfirm = true;
    return false;
  }
  if (pendingConfirm) {
    pendingConfirm = false;
    return true;
  }
  return mappedInput.wasReleased(MappedInputManager::Button::Confirm);
}

// Back never depends on the selection, so it is never held back.
bool UiListActivity::backReleased() { return mappedInput.wasReleased(MappedInputManager::Button::Back); }

void UiListActivity::pollTilt() {
  const auto orientation = static_cast<CrossPointOrientation::Value>(renderer.getOrientation());
  halTiltSensor.update(CrossPointTiltPageTurn::TILT_OFF, static_cast<uint8_t>(orientation), false);
  pollRowTilt();
}

bool UiListActivity::acceptsTiltMenuNavigation() const { return listCount() > 0 && allowsTiltMenuNavigation(); }

bool UiListActivity::queueTiltMenuNavigation(const bool up, const bool down) {
  if (!acceptsTiltMenuNavigation() || (!up && !down)) return false;
  queueNavIntent(up ? NavIntent::StepPrev : NavIntent::StepNext);
  return true;
}

void UiListActivity::pollRowTilt() {
  OptionPopup* const popup = tiltPopup();
  const bool popupOpen = popup != nullptr && popup->isActive();
  halTiltSensor.configureVerticalGesture(SETTINGS.tiltMenuNavigation, popupOpen || acceptsTiltMenuNavigation());
  // Measured on the X3 22/09: the gesture the sensor labels Up is the one readers use to go down
  // a row, so the two readings trade places here.
  const bool up = halTiltSensor.wasTiltedDown();
  const bool down = halTiltSensor.wasTiltedUp();
  if (popupOpen) {
    if (up || down) {
      popup->step(up ? -1 : 1);
      requestUpdate();
    }
    return;
  }
  queueTiltMenuNavigation(up, down);
}

void UiListActivity::loop() {
  pollTilt();
  loopInput();
  // Apply what this pass queued right away when the panel is idle, so a press
  // is reflected before the next pass instead of ten milliseconds later.
  if (navQueueCount > 0) applyPendingNav();
}

void UiListActivity::loopInput() {
  // The render task owns the lock for a whole frame (panel refresh included),
  // so the queue is drained with a non-waiting lock and the buttons are read
  // on every pass either way. A press landing mid-frame is queued, not lost.
  const bool panelFree = applyPendingNav();

  if (!pendingFavorite.empty()) {
    if (!panelFree) return;  // a one-shot wish, safe to retry next pass
    const std::string key = std::move(pendingFavorite);
    pendingFavorite.clear();
    const int row = focusFavorite(key);
    if (row >= 0 && activateFavorite) activateIndex(row);
    requestUpdate();
    return;
  }
  if (rowMenu.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (handleCustomInput()) return;
  if (pendingPin || (supportsFavorites() && !favoriteKey(favoriteSelectedRow()).empty() &&
                     mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 700))) {
    // A hold fires once, so it is never dropped: it is applied as soon as the
    // panel frees the lock.
    RenderLock lock(RenderLock::TryTake{});
    if (!lock.acquired()) {
      pendingPin = true;
      return;
    }
    pendingPin = false;
    const int row = pendingPinRow >= 0 ? pendingPinRow : favoriteSelectedRow();
    pendingPinRow = -1;
    const std::string key = favoriteKey(row);
    if (!key.empty()) {
      favoriteSaveFailed = !toggleFavorite(row);
      LOG_INF("FAV", "pin %s row=%d ok=%d pins=%u key=%s", pendingPinFromTouch ? "touch" : "select", row,
              favoriteSaveFailed ? 0 : 1, static_cast<unsigned>(menucustom::state().pinCount), key.c_str());
      pendingPinFromTouch = false;
      favoritesChanged();
      requestUpdate();
    }
    return;
  }
  if (handleButtons()) return;
  if (routeFadedTap()) return;
  if (routeListTouch()) return;

  // Touch: a flick turns a page, a slow drag moves the rows the finger travelled (one rule, swipeRows);
  // nothing is drawn when the list cannot move that way.
  if (tenorchrome::kTouchShell) {
    auto& n = activeNav();
    const int count = listCount();
    // A faded list keeps the last row of a page as the faded first row of the next.
    const int rows = swipeRows(mappedInput, n, count, ACTION_ROW, fadeKeepRows());
    if (rows == 0) {
      navigateButtons();
      return;
    }
    if ((rows > 0 && n.top + n.pageRowsFor(count) < count) || (rows < 0 && n.top > 0)) {
      n.requestScroll(rows);
      requestUpdate();
    }
    return;
  }
  // Swipes scroll the viewport; the selection stays put (it may scroll
  // off-screen) and button navigation pulls the view back to it.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    queueNavIntent(swipe == MappedInputManager::SwipeDir::Up ? NavIntent::PageNext : NavIntent::PagePrev);
    return;
  }

  navigateButtons();
}

void UiListActivity::moveListPage(const int direction) {
  queueNavIntent(direction > 0 ? NavIntent::PageNext : NavIntent::PagePrev);
}

void UiListActivity::moveToVisibleBoundary(const bool last, const bool ring) {
  // The ring variant has no data work of its own: it only addresses the row as
  // a ring position (UiTabListActivity's hold-to-jump).
  if (ring) {
    queueNavIntent(last ? NavIntent::BoundaryLastRing : NavIntent::BoundaryFirstRing);
    return;
  }
  queueNavIntent(last ? NavIntent::BoundaryLast : NavIntent::BoundaryFirst);
}

void UiListActivity::navigateButtons() {
  using Button = MappedInputManager::Button;
  constexpr unsigned long HOLD_MS = 700;
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
    queueNavIntent(NavIntent::StepNext);
  } else if (mappedInput.wasReleased(Button::Left)) {
    queueNavIntent(NavIntent::StepPrev);
  } else if (mappedInput.wasReleased(Button::Down)) {
    if (!activityManager.switchSettingsSibling(1)) moveListPage(1);
  } else if (mappedInput.wasReleased(Button::Up)) {
    if (!activityManager.switchSettingsSibling(-1)) moveListPage(-1);
  }
}

void UiListActivity::frameRows(fui::ListProps& props) {
  rowsFramed = tenorchrome::roundFrames() && listFramed();
  if (!rowsFramed) {
    // Buttons (X3/X4): the row past the last full one shows its top, faded (fadeMoreBelow), in place of a "more"
    // chevron.
    if (tenorchrome::enabled()) {
      props.partialTrailingRow = true;
      // The fade leaves a row's words legible only in its first 3/4: less of the next row than that and the
      // last row that fits is the glimpse instead (measured on the real rows).
      props.partialTrailingMinPercent = 75;
      // The last page reaches the foot: its last row ends less than a row above it.
      props.fillLastPage = true;
    }
    return;
  }
  // Touch (C1): the rows sit in a round frame 16 px in from the screen edges, their text 16 px into it.
  props.rowInset = tenorchrome::FOOT_BACK_X;
  props.sidePadding = 16;
  // The value in use in a list of choices (ListItem::chosen): bold, with the tick at the row's end.
  props.chosenMark = fui::bitmapFromIcon(icon_row_chosen_24);
  fui::ListItem first;
  if (props.rowProvider && props.count > 0) props.rowProvider(props.rowProviderCtx, props.topIndex, first);
  rowsHaveIcons = props.rowProvider ? static_cast<bool>(first.icon) : props.items && props.count > 0 && props.items[0].icon;
  // The edges fade where rows go on (drawRowFrame), in place of the scroll bar; the row past the last
  // full one shows its top in the bottom band.
  props.scrollIndicator = false;
  props.partialTrailingRow = true;
}

namespace {
using tenorchrome::fadeBand;
}  // namespace

UiListActivity::RowFrameLines UiListActivity::rowFrameLines(const int rowGap) {
  // A 1 px rule in the gap and a 2 px ring: the rule sits `rule` px above the next row, the ring's inner edge as far
  // from the first and last rows as a rule is from the rows beside it.
  const int rule = (std::max(0, rowGap) + 1) / 2;
  return {rule, rule + 1, std::max(0, rowGap) - rule + 2};
}

void UiListActivity::reserveRowFrame(UiScreen& screen, const int rowGap) {
  if (rowsFramed) {
    // The gap list() lays the rows out with: on touch boards the theme's touch gap is the least (resolveListProps).
    rowFrameGap = std::max<int>(rowGap, screen.theme().listTouchRowGap);
    const auto lines = rowFrameLines(rowFrameGap);
    screen.takeTop(static_cast<int16_t>(lines.top));
    rowFrameFloor = screen.body().y + screen.body().height;
    screen.takeBottom(static_cast<int16_t>(lines.bottom));
    return;
  }
  rowFrameFloor = screen.body().y + screen.body().height;
  // The fade under the last full row stops short of the scroll bar at the body's right edge, with 2 px of air.
  rowFadeRight = screen.body().x + screen.body().width - screen.theme().listScrollWidth - 2;
}

void UiListActivity::drawRowFrame(const RowFrameStyle& style) {
  fadeTopFrom = fadeTopTo = fadeFootFrom = fadeFootTo = 0;
  if (!rowsFramed) return;
  const auto lines = rowFrameLines(rowFrameGap);
  // Rows on a page that scrolls under the chrome (Stats): the frame is cut to their band.
  const auto clip = renderer.getClipRect();
  if (style.clipBottom > style.clipTop)
    renderer.setClipRect(0, style.clipTop, renderer.getScreenWidth(), style.clipBottom - style.clipTop);
  // The rows this layout drew and registered (a partial row at the foot registers none; a disabled
  // row registers none either, the frame takes it in by the pitch of the others).
  const auto& n = activeNav();
  const int count = std::min(listCount(), n.top + n.pageRowsFor(listCount()));
  fui::Rect first{}, last{};
  int firstIndex = -1, lastIndex = -1;
  int groupTop = 0;  // a page with groups: the ring top of the group still open
  bool grouped = false;
  int barTop = 0, barBottom = 0;  // a page with groups: the tallest closed frame, home of the scroll bar
  for (int i = n.top; i < count; ++i) {
    const fui::Rect r = app.publishedRect(ACTION_ROW, static_cast<int16_t>(i));
    if (r.height <= 0) continue;
    if (first.height <= 0) {
      first = r;
      firstIndex = i;
      groupTop = r.y - lines.top;
    } else if (rowStartsGroup(i)) {
      // The group above ends at its last row; its heading stands between the 2 frames.
      const int groupBottom = last.y + last.height + lines.bottom;
      tenorchrome::drawPanel(renderer, groupTop, groupBottom - groupTop);
      if (groupBottom - groupTop > barBottom - barTop) {
        barTop = groupTop;
        barBottom = groupBottom;
      }
      groupTop = r.y - lines.top;
      grouped = true;
    } else if (i > n.top) {
      // Grey dotted rule from the text's edge, over every row but the first.
      tenorchrome::drawRowRule(renderer, r.y - lines.rule, tenorchrome::FOOT_BACK_X + 16 + (rowsHaveIcons ? 41 : 0),
                  renderer.getScreenWidth() - tenorchrome::FOOT_BACK_X - 17);
    }
    last = r;
    lastIndex = i;
  }
  if (first.height <= 0) {
    renderer.setClipRect(clip[0], clip[1], clip[2], clip[3]);
    return;
  }
  // A page with groups has headings between its rows, so no one pitch.
  const int pitch = lastIndex > firstIndex ? (last.y - first.y) / (lastIndex - firstIndex) : first.height;
  if (!grouped) {
    first.y = static_cast<int16_t>(first.y - (firstIndex - n.top) * pitch);
    last.height = static_cast<int16_t>(last.height + (count - 1 - lastIndex) * pitch);
  }
  const int ringTop = first.y - lines.top;
  const int lastTop = grouped ? groupTop : ringTop;
  const int fullBottom = last.y + last.height + lines.bottom;
  const bool more = count < listCount();
  const int floor = std::min(renderer.getScreenHeight() - tenorchrome::footBackReserve(), rowFrameFloor);
  // Rows after the page: the frame goes on to the list's foot around the next row's top, and both fade there
  // (founder 06/10: no row outside its frame). A rule still parts it from the last full row.
  // A next row that opens a group closes this frame at the last full row; its heading shows in the fade.
  const bool frameGoesOn = more && !rowStartsGroup(count);
  const int ringBottom = frameGoesOn ? std::max(fullBottom, floor) : fullBottom;
  tenorchrome::drawPanel(renderer, lastTop, ringBottom - lastTop);
  if (frameGoesOn && floor > fullBottom)
    tenorchrome::drawRowRule(renderer, last.y + last.height + rowFrameGap - lines.rule, tenorchrome::FOOT_BACK_X + 16 + (rowsHaveIcons ? 41 : 0),
                renderer.getScreenWidth() - tenorchrome::FOOT_BACK_X - 17);
  // The scroll bar inside the frame's full rows, the shared drawer's round-frame form. A page of several frames
  // keeps it inside the tallest of them, never across the gaps between frames (rule 13).
  if (n.top > 0 || more) {
    const int full = count - n.top;
    if (!grouped || fullBottom - lastTop > barBottom - barTop) {
      barTop = grouped ? lastTop : ringTop;
      barBottom = fullBottom;
    }
    fui::drawListScrollIndicator(uiTarget,
                                 fui::Rect{static_cast<int16_t>(tenorchrome::FOOT_BACK_X), static_cast<int16_t>(barTop),
                                           static_cast<int16_t>(renderer.getScreenWidth() - 2 * tenorchrome::FOOT_BACK_X),
                                           static_cast<int16_t>(barBottom - barTop)},
                                 static_cast<uint32_t>(listCount()), static_cast<uint32_t>(std::max(1, full)),
                                 static_cast<uint32_t>(n.top), 6, 0, 6, tenorchrome::PANEL_RADIUS);
  }
  // Rows before: the first row (the last full row of the page before, a flick keeps it) and its part of the
  // frame fade. Rows after: the frame's part under the last full row fades with the next row's top.
  constexpr int TOP_BAND = 64;
  if (n.top > 0) {
    fadeBand(renderer, ringTop, TOP_BAND, true);
    fadeTopFrom = ringTop;
    fadeTopTo = first.y + first.height;
  }
  if (more && floor > fullBottom) {
    fadeBand(renderer, fullBottom, floor - fullBottom, false);
    fadeFootFrom = fullBottom;
    fadeFootTo = floor;
  }
  renderer.setClipRect(clip[0], clip[1], clip[2], clip[3]);
}

void UiListActivity::syncListViewport(UiScreen& screen, fui::ListProps& props, const bool hasSubtitle) {
  frameRows(props);
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
    // A row with a subtitle grows to its text plus this padding, so the selected pill's ring clears a descender.
    props.rowPaddingY = TENOR_PILL_ROW_PADDING_Y;
  }
  const int rowGap = props.rowGap >= 0 ? props.rowGap : screen.theme().listRowGap;

  if (tenorchrome::kTouchShell && activeNav().followOnBuild) {
    // Touch: a row to show (the chapter being read) brings the page it is on, pages counted from the
    // first row. The page size is the one laid out, so renderUi applies it after the first pass.
    pageAnchorRow = kepConTro(activeNav().selected, listCount());
    activeNav().followOnBuild = false;
  }
  reserveRowFrame(screen, rowGap);
  // Touch: the rows a page holds are counted with the height and gap list() lays them out with (56 + 6 on
  // the X4 Pro), not the theme's touch target (74): the follow and the end clamp land on the real last page.
  const auto laid = rowsFramed ? screen.resolveListProps(props) : props;
  activeNav().syncToProps(screen.body(), rowsFramed ? laid.rowHeight : rowHeight, rowsFramed ? laid.rowGap : rowGap,
                          listCount(), props);
  keepUglyRows(props);

  activeNav().selected = kepConTro(activeNav().selected, listCount());
  // The touch shell has no cursor row: a tap opens or changes the row, nothing waits "selected".
  props.selectedIndex = tenorchrome::kTouchShell ? int16_t{-1} : static_cast<int16_t>(activeNav().selected);
}

void UiListActivity::renderUi() {
  tabBandDrawn = false;
  favoriteHintY = -1;
  // Only a list built in this paint pass owns a row frame.
  rowsFramed = false;
  UiAppHost::renderUi();
  restorePinnedRows();
  if (pageAnchorRow >= 0) {
    // A row already on screen leaves the list where it is (a list coming back to its place).
    auto& n = activeNav();
    const int rows = std::max(1, n.pageRowsFor(listCount()));
    // Pages as a flick turns them: a faded list keeps one row of the page before.
    const int step = rows > fadeKeepRows() ? rows - fadeKeepRows() : rows;
    if (pageAnchorRow < n.top || pageAnchorRow >= n.top + rows) {
      n.top = pageAnchorRow / step * step;
      n.rebuildNeeded = true;
    }
    pageAnchorRow = -1;
  }
  if (!uiTarget.paintingEnabled()) return;
  drawRowFrame(rowFrameStyle());
  if (tenorchrome::enabled() && !tenorchrome::kTouchShell) fadeMoreBelow();
  drawPageHints();
  if (favoriteHintY >= 0) {
    if (const char* hint = favoriteHintText()) tenorchrome::drawTip(renderer, hint, favoriteHintLinesAbove());
  }
}

void UiListActivity::drawPageHints() {
  if (!tenorchrome::enabled() || tenorchrome::kTouchShell || !SETTINGS.tenorSideArrows || !showsSideArrows()) return;
  constexpr int cy = 195;
  const int right = renderer.getScreenWidth() - 1 - 4;  // mirror of column 4 on the left
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

void UiListActivity::keepUglyRows(const fui::ListProps& props) {
  uglyItems_ = props.items;
  uglyItemsFirst_ = props.itemsWindowFirst;
  uglyRowProvider_ = props.rowProvider;
  uglyRowCtx_ = props.rowProviderCtx;
}

namespace {
// A run of words the list laid out, kept to be written by hand where it stands.
struct UglyRun {
  fui::Rect rect;
  std::string text;
  fui::TextAlign align;
  bool locked;
  uint8_t lines;
};
}  // namespace

bool UiListActivity::renderUglyList() {
  if (!uglySkin() || !shell::uglyParts()) return false;
  // The list lays out as usual with its own ink off and its words kept; what a screen draws past the list stays.
  std::vector<UglyRun> runs;
  renderer.clearScreen();
  uiTarget.setPaintingEnabled(false);
  uiTarget.setTextSink(
      [](void* ctx, const fui::Rect rect, const char* text, const fui::TextStyle& style) {
        // A disabled row's words come in light gray: the row is locked.
        static_cast<std::vector<UglyRun>*>(ctx)->push_back(
            {rect, text, style.align, !style.inverted && style.color == fui::Color::LightGray, style.maxLines});
      },
      &runs);
  renderSettledList(activeNav(), [&] {
    runs.clear();
    renderUi();
  });
  uiTarget.setTextSink(nullptr, nullptr);
  uiTarget.setPaintingEnabled(true);
  drawChrome();
  std::vector<fui::Rect> ink;
  ink.reserve(runs.size());
  for (const auto& run : runs)
    ink.push_back(uglychrome::words(renderer, run.rect, run.text.c_str(), run.align, run.locked, run.lines));
  // The marks go on the rows that took a place on screen; a locked row has no place to choose and gets none.
  const int count = listCount(), first = std::max(0, activeNav().top);
  for (int row = first; row < count && row < first + 64; ++row) {
    const auto box = app.publishedRect(ACTION_ROW, static_cast<int16_t>(row));
    if (box.empty()) continue;
    fui::ListItem item;
    if (uglyRowProvider_) uglyRowProvider_(uglyRowCtx_, static_cast<uint16_t>(row), item);
    else if (uglyItems_ && row >= uglyItemsFirst_) item = uglyItems_[row - uglyItemsFirst_];
    uglychrome::Marks marks{row == activeNav().selected, item.chosen, item.opensNext, item.toggle, item.toggleChecked};
    // The circle goes round the row's label: the first words written from the row's left half.
    for (size_t i = 0; marks.selected && i < runs.size(); ++i) {
      const auto& at = runs[i].rect;
      const int mid = at.y + at.height / 2;
      if (runs[i].align == fui::TextAlign::Left && mid >= box.y && mid < box.bottom() && at.x < box.x + box.width / 2) {
        marks.around = ink[i];
        break;
      }
    }
    uglychrome::marks(renderer, box, marks);
  }
  drawFooter();
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "part=rows runs=%d", static_cast<int>(runs.size()));
#endif
  return true;
}

void UiListActivity::render(RenderLock&&) {
  if (rowMenu.processRender(renderer, mappedInput)) return;
  if (renderUglyList()) {
    renderer.displayBuffer();
    return;
  }
  renderSettledList(activeNav(), [&] {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  });
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
  const int top = tabBandDrawn ? tenorchrome::contentTopUnderTabs() : tenorchrome::contentTop();
  if (tenorchrome::enabled() && screen.body().y < top) screen.takeTop(static_cast<int16_t>(top - screen.body().y));
  // Touch: the rows stop above the round back button at the foot, where a screen has one.
  if (!tabBandDrawn && tenorchrome::wantsFootBack(name.c_str())) {
    const int bottom = screen.body().y + screen.body().height;
    const int limit = renderer.getScreenHeight() - tenorchrome::footBackReserve();
    if (bottom > limit) screen.takeBottom(static_cast<int16_t>(bottom - limit));
  }
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
  if (!supportsFavorites() || SETTINGS.globalStatusBarHidden()) return;
  const char* hint = favoriteHintText();
  if (!hint || !tenorchrome::tipShown(hint)) return;
  favoriteHintY = tenorchrome::tipTopY(renderer, hint, favoriteHintLinesAbove());
  const int bottom = screen.body().y + screen.body().height;
  const int reservedTop = favoriteHintY - 2;
  if (bottom > reservedTop) screen.takeBottom(static_cast<int16_t>(bottom - reservedTop));
}

void UiListActivity::fadeMoreBelow() {
  // Buttons: rows go on below, so the band under the last full row fades over the next row's top, down to the
  // list's foot (as X4 Pro's framed lists do). The scroll bar beside it stays whole.
  const auto& n = activeNav();
  const int count = listCount();
  const int last = n.top + n.pageRowsFor(count) - 1;
  if (last < 0 || last + 1 >= count) return;
  const fui::Rect r = app.publishedRect(ACTION_ROW, static_cast<int16_t>(last));
  if (r.height <= 0) return;  // a disabled last row registers no rect: the scroll bar alone says there is more
  const int y0 = r.y + r.height;
  fadeBand(renderer, y0, rowFrameFloor - y0, false, 0, rowFadeRight);
}

const char* UiListActivity::favoriteHintText() {
  const std::string key = favoriteKey(favoriteSelectedRow());
  if (key.empty() && !favoriteSaveFailed) return nullptr;
  const StrId hint = favoriteSaveFailed                           ? StrId::STR_MENU_SAVE_FAILED
                     : menucustom::state().find(key.c_str()) >= 0 ? StrId::STR_MENU_PINNED
                                                                  : StrId::STR_MENU_PIN_HINT;
  return I18N.get(hint);
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
