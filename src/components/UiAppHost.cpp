#include "UiAppHost.h"

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "TouchScroll.h"
#include "UiAppHelpers.h"

namespace fui = freeink::ui;

UiAppHost::UiAppHost(const GfxRenderer& renderer)
    : uiTarget(makeUiTarget(renderer)), app(uiTarget, uiTarget.deviceContext()) {}

void UiAppHost::resetUi() {
  uiReady = false;
  applySharedUiTheme(app, uiTarget);
}

void UiAppHost::renderUi() {
  app.setDevice(uiTarget.deviceContext());
  app.render();
  uiReady = true;
}

UiAppHost::TouchRoute UiAppHost::routeTouch(const MappedInputManager& input, const bool withLongPress,
                                            const bool routeHeld) {
  TouchRoute result;  // named apart from route() — cppcheck flags the shadow
  cancelStaleTouch(input);
  if (!uiReady) return result;
  result.snap = touchSnapshotFrom(input, withLongPress);
  if (!result.snap.touchPressed && !result.snap.touchReleased && !(routeHeld && result.snap.touchHeld)) {
    return result;
  }
  result.routed = true;
  result.event = routeApp(result.snap);
  return result;
}

void UiAppHost::cancelStaleTouch(const MappedInputManager& input) {
  if (touchContactGeneration == input.touchContactGeneration()) return;
  touchContactGeneration = input.touchContactGeneration();
  app.cancelTouchContact();
}

fui::ActionEvent UiAppHost::route(const fui::InputSnapshot& snap, const MappedInputManager& input) {
  cancelStaleTouch(input);
  if (!uiReady) return {};
  return routeApp(snap);
}

// Touch: the tap being dispatched (a screen opens a value list from its handler), else the rect of the last
// tap the routing dispatched, for the screen in front when it came (a screen acting on a tap a pass after
// routing it: the reader menu reports it, the reader opens the list).
namespace {
const UiAppHost::UiApp* dispatching = nullptr;
fui::Rect lastTapRect{};
uint32_t lastTapScreen = UINT32_MAX;
}  // namespace

fui::ActionEvent UiAppHost::routeApp(const fui::InputSnapshot& snap) {
  dispatching = &app;
  const fui::ActionEvent event = app.route(snap);
  dispatching = nullptr;
  if (event) {
    lastTapRect = app.publishedRect(event.action, event.value);
    lastTapScreen = activityManager.activityGeneration();
  }
  return event;
}

fui::Rect UiAppHost::dispatchingRect() {
  if (dispatching) {
    const fui::ActionEvent event = dispatching->lastEvent();
    return event ? dispatching->publishedRect(event.action, event.value) : fui::Rect{};
  }
  return lastTapScreen == activityManager.activityGeneration() ? lastTapRect : fui::Rect{};
}

int UiAppHost::swipeRows(const MappedInputManager& input, const fui::ListNav& nav, const int count,
                         const fui::ActionId rowAction, const int keepRows) const {
  int dy = 0;
  unsigned long heldMs = 0;
  if (!input.wasVerticalSwipe(dy, heldMs)) return 0;
  const int page = nav.pageRowsFor(count);
  // The pitch between the first and the last row of the page that registered a touch (a disabled row
  // registers none).
  fui::Rect first{}, last{};
  int firstIndex = -1, lastIndex = -1;
  for (int i = nav.top; i < nav.top + page && i < count; ++i) {
    const fui::Rect r = app.publishedRect(rowAction, static_cast<int16_t>(i));
    if (r.height <= 0) continue;
    if (firstIndex < 0) {
      first = r;
      firstIndex = i;
    }
    last = r;
    lastIndex = i;
  }
  const int pitch = lastIndex > firstIndex ? (last.y - first.y) / (lastIndex - firstIndex) : first.height;
  return touchscroll::rows(dy, heldMs, pitch, page > keepRows + 1 ? page - keepRows : page);
}
