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
