#include "ConfirmationActivity.h"

#include <I18n.h>
#include <Logging.h>

#include "HalDisplay.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "shells/Shell.h"
#include "shells/ugly/UglySwitch.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body)
    : Activity("Confirmation", renderer, mappedInput), heading(heading), body(body) {}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();
  if (shell::isUgly()) ugly::ensureFonts(renderer);

  // Both texts live inside the dialog: the heading as its caption and the
  // subject (a book title) as the wrapping headline beneath it. No
  // pre-truncation, the dialog wraps both to its own width.
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
  if (shell::isUgly()) {
    // The question box of the shell: the heading asked in a large hand, the subject under it, No first in the circle.
    const char* answers[2] = {I18N.get(StrId::STR_CONFIRM), I18N.get(StrId::STR_CANCEL)};
    ugly::askBox(renderer, mappedInput, heading.c_str(), body.c_str(), answers, uglyChoice, uglyAnswers, mark_.get(uglyChoice));
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
  if (shell::isUgly()) {
    using B = MappedInputManager::Button;
    if (tenorchrome::kTouchShell) {
      const auto input = touchSnapshotFrom(mappedInput);
      int answer = -1;
      if (input.touchReleased && input.touchX >= 0) {
        RenderLock lock(RenderLock::TryTake{});
        if (!lock.acquired()) return;
        for (int index = 0; index < 2; ++index) {
          const auto& box = uglyAnswers[index];
          const int top = (box.y0 + box.y1 - 66) / 2;
          if (box.x1 > box.x0 && input.touchY >= top && input.touchY < top + 66)
            answer = index;
        }
      }
      if (answer >= 0) {
        ActivityResult result;
        result.isCancelled = answer != 0;
        setResult(std::move(result));
        finish();
        return;
      }
    }
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
