#pragma once

#include <cstdint>

// Button edges collected by the sample timer between two main-loop frames.
// Plain data: the owner serialises add() and take().
//
// Edges are counted per button, not OR-ed: the main loop can stall for seconds
// (a heap-starved section build while the radio starts) and every tap the timer
// saw meanwhile must still reach the reader. take() hands out at most one press
// and one release per button, so consumers that read one edge per frame see
// each tap in a frame of its own.
struct ButtonEdgeLatch {
  static constexpr int kButtons = 7;
  static constexpr int kMaxPending = 8;
  uint8_t pressed[kButtons] = {};
  uint8_t released[kButtons] = {};

  void add(const uint8_t pressedMask, const uint8_t releasedMask) {
    for (int b = 0; b < kButtons; ++b) {
      if ((pressedMask >> b & 1u) && pressed[b] < kMaxPending) ++pressed[b];
      if ((releasedMask >> b & 1u) && released[b] < kMaxPending) ++released[b];
    }
  }

  // Edges for one frame.
  void take(uint8_t& pressedMask, uint8_t& releasedMask) {
    pressedMask = releasedMask = 0;
    for (int b = 0; b < kButtons; ++b) {
      if (pressed[b]) {
        --pressed[b];
        pressedMask |= 1u << b;
      }
      if (released[b]) {
        --released[b];
        releasedMask |= 1u << b;
      }
    }
  }

  bool pending() const {
    for (int b = 0; b < kButtons; ++b) {
      if (pressed[b] | released[b]) return true;
    }
    return false;
  }
};

// The buttons as the sample timer last read them: which are down, whether the
// debounce is still weighing a contact, and the held times at that read.
struct ButtonSample {
  uint8_t state = 0;
  bool debouncePending = false;
  unsigned long heldMs = 0;
  unsigned long powerHeldMs = 0;
  unsigned long atMs = 0;
};

// Everything the sample timer hands the main loop: the edges it collected and
// its latest sample. Plain data: the owner serialises every call.
//
// A pass of the main loop takes its edges and a copy of the latest sample in one
// step and answers levels and held times from that copy until the next pass. Read
// live instead, a release the timer sees after the pass took its edges shows up as
// "not held" with no release edge yet: code that waits for a button to be let go
// (the swallowed release after a hold to wake) takes that pass, and the release then
// arrives on the next pass as a fresh short press.
struct ButtonFrames {
  static constexpr uint8_t kPowerButton = 6;
  ButtonEdgeLatch edges;
  ButtonSample latest;
  ButtonSample frame;
  uint8_t framePressed = 0;
  uint8_t frameReleased = 0;

  // Timer side, after each debounce step.
  void publish(const uint8_t pressedMask, const uint8_t releasedMask, const ButtonSample& now) {
    edges.add(pressedMask, releasedMask);
    latest = now;
  }
  // Main loop, once per pass: the edges and the levels this pass handles.
  void beginFrame() {
    edges.take(framePressed, frameReleased);
    frame = latest;
  }

  bool isPressed(const uint8_t button) const { return (frame.state >> button) & 1u; }
  // A held time keeps growing between samples, as the SDK's own getter would when read live.
  unsigned long heldTime(const unsigned long nowMs) const {
    return frame.state ? frame.heldMs + (nowMs - frame.atMs) : frame.heldMs;
  }
  unsigned long powerHeldTime(const unsigned long nowMs) const {
    return (frame.state >> kPowerButton) & 1u ? frame.powerHeldMs + (nowMs - frame.atMs) : frame.powerHeldMs;
  }
  // Idle wake: a collected edge, or a contact down or still being weighed right now.
  bool active() const { return edges.pending() || latest.debouncePending || latest.state != 0; }
};
