#include "ConfirmationActivity.h"

#include <I18n.h>
#include <Logging.h>

#include "HalDisplay.h"
#include "components/UITheme.h"
#include "shells/Shell.h"
#include "shells/ugly/UglySwitch.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body)
    : Activity("Confirmation", renderer, mappedInput), heading(heading), body(body) {}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();

  // Both texts live inside the dialog: the heading as its caption and the
  // subject (a book title) as the wrapping headline beneath it. No
  // pre-truncation — the dialog wraps both to its own width.
  const char* options[] = {I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM)};
  confirmPopup.show(heading.c_str(), body.c_str(), options, 2, 0, [this](int idx) {
    ActivityResult res;
    res.isCancelled = (idx != 1);
    setResult(std::move(res));
    finish();
  });

  requestUpdate(true);
}

void ConfirmationActivity::render(RenderLock&& lock) {
  if (shell::uglyParts()) {
    // The question box of the shell: the heading asked in a large hand, the subject under it, No first in the circle.
    const char* answers[2] = {I18N.get(StrId::STR_CONFIRM), I18N.get(StrId::STR_CANCEL)};
    ugly::Box drawn[2];
    ugly::askBox(renderer, mappedInput, heading.c_str(), body.c_str(), answers, uglyChoice, drawn);
    renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
#ifdef UGLY_FRAME_LOG
    LOG_INF("UGLY", "part=ask sel=%d", uglyChoice);
#endif
    return;
  }
  renderer.clearScreen();

  if (confirmPopup.processRender(renderer, mappedInput)) return;

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void ConfirmationActivity::loop() {
  if (shell::uglyParts()) {
    using B = MappedInputManager::Button;
    if (mappedInput.wasReleased(B::Up) || mappedInput.wasReleased(B::Down) || mappedInput.wasReleased(B::Left) ||
        mappedInput.wasReleased(B::Right)) {
      uglyChoice = 1 - uglyChoice;
      requestUpdate();
      return;
    }
    const bool yes = mappedInput.wasReleased(B::Confirm) && uglyChoice == 0;
    if (!yes && !mappedInput.wasReleased(B::Confirm) && !mappedInput.wasReleased(B::Back)) return;
    ActivityResult res;
    res.isCancelled = !yes;
    setResult(std::move(res));
    finish();
    return;
  }
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Popup dismissed without a selection (Back button or tap outside): cancel.
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}
