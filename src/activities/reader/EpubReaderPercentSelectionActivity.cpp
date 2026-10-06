#include "EpubReaderPercentSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "components/UiSliderDialog.h"
#include "ReaderToolbarUi.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_SLIDER = 1;
constexpr fui::ActionId ACTION_STEP = 2;
constexpr fui::ActionId ACTION_OK = 4;
constexpr fui::ActionId ACTION_CHROME = 5;  // absorbs taps on the dialog body; no handler
// Fine/coarse step sizes for percent adjustments (buttons and -/+ tap zones).
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;
}  // namespace

EpubReaderPercentSelectionActivity::EpubReaderPercentSelectionActivity(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput,
                                                                       const int initialPercent)
    : Activity("EpubReaderPercentSelection", renderer, mappedInput), UiAppHost(renderer), percent(initialPercent) {}

void EpubReaderPercentSelectionActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_SLIDER, &EpubReaderPercentSelectionActivity::onSliderEvent, this);
  app.on(ACTION_STEP, &EpubReaderPercentSelectionActivity::onStepEvent, this);
  app.on(ACTION_OK, &EpubReaderPercentSelectionActivity::onOkEvent, this);
  app.setScreen(&EpubReaderPercentSelectionActivity::percentScreen, this);
  // Set up rendering task and mark first frame dirty.
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderPercentSelectionActivity::adjustPercent(const int delta) {
  // Wrap using a 100-value ring (0% and 100% are the same wrap point), but keep 100 as the
  // natural landing value when reached without crossing the boundary (e.g. 90 + 10 = 100).
  const int raw = percent + delta;
  if (raw > 0 && raw % 100 == 0) {
    percent = 100;
  } else {
    percent = ((raw % 100) + 100) % 100;
  }
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::setPercent(const int value) {
  const int clamped = std::clamp(value, 0, 100);
  if (clamped == percent) return;
  percent = clamped;
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::onSliderEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<EpubReaderPercentSelectionActivity*>(user);
  if (event.dragPermille < 0) return;
  self->setPercent((static_cast<int>(event.dragPermille) * 100 + 500) / 1000);
}

void EpubReaderPercentSelectionActivity::onStepEvent(const fui::ActionEvent& event, void* user) {
  static_cast<EpubReaderPercentSelectionActivity*>(user)->adjustPercent(event.value * kSmallStep);
}

void EpubReaderPercentSelectionActivity::onOkEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<EpubReaderPercentSelectionActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->confirm();
}

void EpubReaderPercentSelectionActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void EpubReaderPercentSelectionActivity::confirm() {
  setResult(PercentResult{percent});
  finish();
}

void EpubReaderPercentSelectionActivity::loop() {
  // Touch goes through the FreeInkApp: render() registered the slider and -/+ hit
  // rects; the slider follows the finger via InputDrag (dragPermille per held frame).
  // Runs before the Back handler because the release of a drag can also register as a
  // swipe (e.g. the left-edge rightward back gesture) — the drag must consume it so it
  // can't cancel the dialog or step the percent.
  const auto route = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) {
    if (route.event.dragPermille >= 0) draggingSlider = true;
    return;
  }
  if (routingReady() && draggingSlider) {
    // Drag ended (possibly off the slider): swallow the tap/swipe events it produced.
    if (!route.snap.touchHeld) draggingSlider = false;
    return;
  }
  // Tap released outside the dialog (inside-taps are absorbed by the chrome
  // guard): cancel. Swipe-end releases arrive with -1,-1 coords and fall
  // through — same rule as OptionPopup.
  if (route.routed && route.snap.touchReleased && route.snap.touchX >= 0) {
    cancel();
    return;
  }

  // Back cancels, confirm selects, arrows adjust the percent.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Right) {
    adjustPercent(kLargeStep);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Left) {
    adjustPercent(-kLargeStep);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirm();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustPercent(-kSmallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustPercent(kSmallStep); });

  // On edge-button boards (X3, X4 Pro) the side buttons sit on the left/right edges of the screen rather
  // than as a vertical up/down rocker (X4), so BTN_UP is physically the left button and BTN_DOWN the right
  // one. Flip the large-step direction there so the left button decreases and the right button increases.
  const int upDelta = gpio.hasEdgeSideButtons() ? -kLargeStep : kLargeStep;
  const int downDelta = gpio.hasEdgeSideButtons() ? kLargeStep : -kLargeStep;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, upDelta] { adjustPercent(upDelta); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [this, downDelta] { adjustPercent(downDelta); });
}

void EpubReaderPercentSelectionActivity::percentScreen(UiScreen& screen, void* user) {
  static_cast<EpubReaderPercentSelectionActivity*>(user)->buildPercentScreen(screen);
}

void EpubReaderPercentSelectionActivity::buildPercentScreen(UiScreen& screen) {
  char readout[16];
  snprintf(readout, sizeof(readout), "%d%%", percent);
  char hint1[64];
  snprintf(hint1, sizeof(hint1), "%s %d%%", I18N.get(StrId::STR_STEP_HINT_FRONT), kSmallStep);
  char hint2[64];
  snprintf(hint2, sizeof(hint2), "%s %d%%", I18N.get(StrId::STR_STEP_HINT_SIDE), kLargeStep);

  UiSliderDialogSpec spec;
  spec.title = tr(STR_GO_TO_PERCENT);
  spec.readout = readout;
  spec.value = percent;
  spec.max = 100;
  spec.minLabel = "0%";
  spec.maxLabel = "100%";
  spec.sliderAction = ACTION_SLIDER;
  spec.stepAction = ACTION_STEP;
  spec.okAction = ACTION_OK;
  spec.chromeAction = ACTION_CHROME;
  spec.hintLine1 = hint1;
  spec.hintLine2 = hint2;
  buildSliderDialogScreen(screen, uiTarget, mappedInput, spec);
}

namespace {
// tenor/ugly on the button readers: a scrap of paper over the menu, the number written large over a ruler in pen,
// a jab that follows where the reader wants to go.
void renderUgly(const GfxRenderer& r, const int percent, const char* hint1, const char* hint2) {
  [[maybe_unused]] const uint32_t started = millis();
  const int w = r.getScreenWidth(), x = 20, right = w - 20, top = r.getScreenHeight() / 2 - 190;
  readerugly::paper(r, {static_cast<int16_t>(x), static_cast<int16_t>(top), static_cast<int16_t>(right - x), 360});
  ugly::text(r, ugly::Size::S30, x + 24, top + 48, tr(STR_UGLY_PCT_TITLE));
  const char* jab = percent == 0 ? tr(STR_UGLY_PCT_START) : percent == 100 ? tr(STR_UGLY_PCT_END) : tr(STR_UGLY_PCT_MID);
  ugly::paragraph(r, ugly::Size::S22, x + 24, top + 82, right - x - 48, 26, jab);
  // The pen has no "%": the number large, the sign after it in the small hand (which falls back to the UI font).
  char number[4];
  snprintf(number, sizeof(number), "%d", percent);
  const int nw = ugly::width(r, ugly::Size::S52, number), sw = ugly::width(r, ugly::Size::S22, "%");
  const int nx = (w - nw - sw - 4) / 2;
  ugly::text(r, ugly::Size::S52, nx, top + 182, number);
  ugly::text(r, ugly::Size::S22, nx + nw + 4, top + 182, "%");
  // The ruler: a shaky line, a tick each 10%, 0, 50 and 100 under theirs, and a pen circle where the reader stands.
  const int x0 = x + 40, x1 = right - 40, cy = top + 228;
  ugly::line(r, x0, cy, x1, cy, 834, 2);
  for (int i = 0; i <= 10; ++i) {
    const int tx = x0 + (x1 - x0) * i / 10, reach = i % 5 ? 6 : 10;
    ugly::line(r, tx, cy - reach, tx, cy + reach, 840 + i, 1);
    if (i % 5) continue;
    char at[4];
    snprintf(at, sizeof(at), "%d", i * 10);
    ugly::text(r, ugly::Size::S22, tx - ugly::width(r, ugly::Size::S22, at) / 2, cy + 36, at);
  }
  const int kx = x0 + (x1 - x0) * percent / 100;
  ugly::circle(r, ugly::Circle::Object, {kx - 7, cy - 7, kx + 7, cy + 7}, 0, 0, 3);
  ugly::text(r, ugly::Size::S22, x + 24, top + 306, hint1);
  ugly::text(r, ugly::Size::S22, x + 24, top + 336, hint2);
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Percent frame value=%d total=%lums", percent, static_cast<unsigned long>(millis() - started));
#endif
}
}  // namespace

void EpubReaderPercentSelectionActivity::render(RenderLock&&) {
  if (shell::uglyParts()) {
    char hint1[64], hint2[64];
    snprintf(hint1, sizeof(hint1), "%s %d%%", I18N.get(StrId::STR_STEP_HINT_FRONT), kSmallStep);
    snprintf(hint2, sizeof(hint2), "%s %d%%", I18N.get(StrId::STR_STEP_HINT_SIDE), kLargeStep);
    renderUgly(renderer, percent, hint1, hint2);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
  // No clearScreen: the dialog is a popup — it dims the frame it opened over
  // and draws the card on top. Everything renders through the app so the
  // slider, -/+ zones, and Cancel/OK register touch hit rects.
  renderUi();

  // Button hints follow the current front button layout.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
