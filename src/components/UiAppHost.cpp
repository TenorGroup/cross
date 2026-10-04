#include "UiAppHost.h"

#include "MappedInputManager.h"
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
  result.event = app.route(result.snap);
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
  return app.route(snap);
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
