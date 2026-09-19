#pragma once
#include <array>
#include <chrono>
#include <cstddef>

class MappedInputManager {
 public:
  enum class Button { Left, Right, Up, Down, Confirm, Back };
  std::array<bool, 6> pressed{}, released{}, held{};
  unsigned long heldMs = 0;
  bool liveHeldClock = false;
  std::chrono::steady_clock::time_point heldClockStart;
  bool tap = false, touchDown = false, touchHeld = false;
  int touchX = 0, touchY = 0;
  bool wasPressed(Button b) const { return pressed[static_cast<size_t>(b)]; }
  bool wasReleased(Button b) const { return released[static_cast<size_t>(b)]; }
  bool isPressed(Button b) const { return held[static_cast<size_t>(b)]; }
  unsigned long getHeldTime() const {
    // Like InputManager: the sampled pressed state is stable until update(),
    // but its held duration keeps advancing while the activity waits to render.
    return heldMs + (liveHeldClock ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - heldClockStart).count() : 0);
  }
  bool wasScreenTapped(int& x, int& y) const { x = touchX; y = touchY; return tap; }
  bool wasScreenTouchDown(int& x, int& y) const { x = touchX; y = touchY; return touchDown; }
  bool isScreenTouchHeld(int& x, int& y) const { x = touchX; y = touchY; return touchHeld; }
  struct Labels { const char *btn1, *btn2, *btn3, *btn4; };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) const { return {a,b,c,d}; }
  void reset() { pressed = {}; released = {}; held = {}; heldMs = 0; liveHeldClock = false; tap = touchDown = touchHeld = false; }
};
