#pragma once

// How far a vertical swipe moves a touch list (dynamic bar rule 11), the one rule for every list.
// A flick (lifted within TOUCH_FLICK_MAX_MS) turns a whole page. A slow drag moves the list by the rows
// the finger travelled, rounded to a row edge, so the row under the finger at the touch comes to rest
// near where it was lifted; one refresh either way. Positive rows bring the rows below into view.
namespace touchscroll {

// Between a flick and a drag. A guess until the founder's thumb is measured on the panel.
constexpr unsigned long TOUCH_FLICK_MAX_MS = 300;

constexpr int rows(const int dy, const unsigned long heldMs, const int rowPitch, const int pageRows) {
  if (dy == 0) return 0;
  const int direction = dy < 0 ? 1 : -1;  // finger up: the list moves up
  const int page = pageRows > 0 ? pageRows : 1;
  if (heldMs <= TOUCH_FLICK_MAX_MS || rowPitch <= 0) return direction * page;
  const int travel = dy < 0 ? -dy : dy;
  const int moved = (travel + rowPitch / 2) / rowPitch;
  return direction * (moved > 0 ? moved : 1);
}

}  // namespace touchscroll
