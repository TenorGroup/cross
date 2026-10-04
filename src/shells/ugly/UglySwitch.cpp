#include "UglySwitch.h"

#include "UglyLogic.h"

#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/Activity.h"

namespace ugly {
namespace {
constexpr int SIDE = 30;       // the paper frame's distance from the edge
constexpr int PAD = 30;        // text from the frame
constexpr int BODY_LINE = 40;
constexpr int OPTION_STEP = 66;
}  // namespace

void SwitchConfirm::onEnter() {
  Screen::onEnter();
  ensureFonts(renderer);
  selected = NO;
  requestUpdate();
}

void SwitchConfirm::render(RenderLock&&) {
  [[maybe_unused]] const uint32_t started = millis();
  renderer.clearScreen();
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  const int x = SIDE + PAD, room = w - 2 * x;
  const char* body = tr(STR_UGLY_SWITCH_ASK);
  const int lines = paragraph(renderer, Size::S30, x, 0, room, BODY_LINE, body, false);
  const int bodyHeight = (lines - 1) * BODY_LINE;
  // Frame top to bottom: padding, the body, a gap, the two options, padding. The whole is centred above the bar.
  const int total = PAD + 30 + bodyHeight + 70 + OPTION_STEP + 30;
  const int top = std::max(40, (h - 110 - total) / 2);
  const int bodyBase = top + PAD + 30;
  const int firstOption = bodyBase + bodyHeight + 80;
  const int bottom = firstOption + OPTION_STEP + 30;

  // One shaky stroke per side, each with its own seed: a sheet of paper torn out and laid down crooked.
  line(renderer, SIDE, top, w - SIDE, top + 3, 701, 2);
  line(renderer, w - SIDE, top + 3, w - SIDE - 4, bottom, 702, 2);
  line(renderer, w - SIDE - 4, bottom, SIDE + 2, bottom - 3, 703, 2);
  line(renderer, SIDE + 2, bottom - 3, SIDE, top, 704, 2);

  paragraph(renderer, Size::S30, x, bodyBase, room, BODY_LINE, body);
  const char* labels[COUNT] = {tr(STR_UGLY_SWITCH_YES), tr(STR_UGLY_SWITCH_NO)};
  Box box[COUNT] = {};
  for (int i = 0; i < COUNT; ++i) {
    const int base = firstOption + i * OPTION_STEP;
    const std::string label = fit(renderer, Size::S38, labels[i], room - 40);
    const int lw = text(renderer, Size::S38, x + 20, base, label.c_str());
    box[i] = {x + 20, base - ascent(Size::S38), x + 20 + lw, base + 10};
  }
  circle(renderer, Circle::Row, box[selected], 14, 10);

  statusBar(renderer, mappedInput, {true, true, true, true});
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
#ifdef UGLY_FRAME_LOG
  LOG_INF("UGLY", "Switch frame total=%lums sel=%d lines=%d frame=%d,%d,%d,%d box=%d,%d,%d,%d", static_cast<unsigned long>(millis() - started),
          selected, lines, SIDE, top, w - SIDE, bottom, box[selected].x0, box[selected].y0, box[selected].x1, box[selected].y1);
#endif
}

void SwitchConfirm::answer(const bool yes) {
  then([this, yes] {
    ActivityResult result;
    result.isCancelled = !yes;
    setResult(std::move(result));
    finish();
  });
}

bool SwitchConfirm::onKey(const Key key) {
  switch (key) {
    case Key::Up:
    case Key::UpHold:
      selected = logic::cycle(selected, -1, COUNT);
      return true;
    case Key::Down:
    case Key::DownHold:
      selected = logic::cycle(selected, 1, COUNT);
      return true;
    case Key::Confirm:
      answer(selected == YES);
      return false;
    case Key::Back:
      answer(false);
      return false;
    default:
      return false;
  }
}

}  // namespace ugly
