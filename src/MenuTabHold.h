#pragma once

// The edge pair of the button boards, held on a menu that has tabs: Up or Down held HOLD_MS moves the tab in focus one
// place in the saved order (menucustom::moveTab). One gesture for the list menu's tab list and the toolbar menu's X3 bar.
namespace menutab {

inline constexpr unsigned long HOLD_MS = 700;

struct Hold {
  bool consumed = false;  // the pass belongs to this gesture: nothing else reads the buttons
  int direction = 0;      // -1 Up, 1 Down: the tab moves; 0 none
};

// `chordBlocked` is the caller's latch: Up and Down pressed together move nothing and keep the passes until both are up.
// The hold fires once per press and swallows the release that ends it (MappedInputManager::wasLongPressed).
template <class Input>
Hold pollHold(const Input& input, bool& chordBlocked) {
  using Button = typename Input::Button;
  const bool up = input.isPressed(Button::Up), down = input.isPressed(Button::Down);
  if (up && down) {
    chordBlocked = true;
    input.wasLongPressed(Button::Up, 0);
    input.wasLongPressed(Button::Down, 0);
  }
  if (chordBlocked) {
    if (!up && !down) chordBlocked = false;
    return {true, 0};
  }
  if (input.wasLongPressed(Button::Up, HOLD_MS)) return {true, -1};
  if (input.wasLongPressed(Button::Down, HOLD_MS)) return {true, 1};
  return {};
}

}  // namespace menutab
