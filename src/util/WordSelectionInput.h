#pragma once

#include <cstdint>

#include "MappedInputManager.h"
#include "activities/reader/WordSelectRepeat.h"

class WordSelectionInput {
 public:
  enum ButtonEvent : uint8_t {
    INPUT_LEFT = 1,
    INPUT_RIGHT = 2,
    INPUT_UP = 4,
    INPUT_DOWN = 8,
    INPUT_CONFIRM = 16,
    INPUT_BACK = 32,
    INPUT_PREVIOUS = 64,
    INPUT_NEXT = 128
  };

  static uint8_t buttonEdges(const MappedInputManager& input) {
    using B = MappedInputManager::Button;
    return (input.wasPressed(B::ScreenLeft) ? INPUT_LEFT : 0) | (input.wasPressed(B::ScreenRight) ? INPUT_RIGHT : 0) |
           (input.wasPressed(B::ScreenUp) ? INPUT_UP : 0) | (input.wasPressed(B::ScreenDown) ? INPUT_DOWN : 0) |
           (input.wasReleased(B::Confirm) ? INPUT_CONFIRM : 0) | (input.wasReleased(B::Back) ? INPUT_BACK : 0) |
           (input.wasPressed(B::NavPrevious) ? INPUT_PREVIOUS : 0) | (input.wasPressed(B::NavNext) ? INPUT_NEXT : 0);
  }

  // A held Left/Right repeats on the ramp of wordselect::Repeat: a pause first, then faster steps.
  uint8_t pollButtons(const MappedInputManager& input, const uint32_t now) {
    uint8_t buttons = buttonEdges(input);
    const bool leftDown = input.isPressed(MappedInputManager::Button::ScreenLeft);
    const bool rightDown = input.isPressed(MappedInputManager::Button::ScreenRight);
    if (buttons & (INPUT_LEFT | INPUT_RIGHT)) {
      repeat.pressed(now);
    } else if (!leftDown && !rightDown) {
      repeat.released();
    } else if (repeat.shouldStep(now)) {
      buttons |= (leftDown ? INPUT_LEFT : 0) | (rightDown ? INPUT_RIGHT : 0);
    }
    return buttons;
  }

 private:
  wordselect::Repeat repeat;
};
