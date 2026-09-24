#pragma once

#include <cstdint>

// The buttons as the sample timer read them: which are down, whether the debounce is
// still weighing a contact, and the held times at that read.
struct ButtonSample {
  uint8_t state = 0;
  bool debouncePending = false;
  unsigned long heldMs = 0;
  unsigned long powerHeldMs = 0;
  unsigned long atMs = 0;
};

// Button edges collected by the sample timer between two main-loop frames, oldest first.
// Plain data: the owner serialises add() and take().
//
// The main loop can stall for seconds (a heap-starved section build while the radio
// starts) and every tap the timer saw meanwhile must still reach the reader, in the
// order it came: the reader checks Previous before Next, so a Next tap and a Previous
// tap handed out in one frame would add up to one page back. take() hands out the
// oldest edges of one button, a press and its release at most, so consumers that read
// one edge per frame see each tap in a frame of its own. Each edge keeps the sample it
// was seen in, so a short tap handed out late still reads its own held time.
struct ButtonEdgeLatch {
  static constexpr int kMaxPending = 16;
  struct Edge {
    uint8_t pressed;
    uint8_t released;
    ButtonSample at;
  };
  Edge queue[kMaxPending];
  uint8_t head = 0;
  uint8_t count = 0;

  // A full queue drops the newest edges: a stuck contact cannot run away.
  void add(const uint8_t pressedMask, const uint8_t releasedMask, const ButtonSample& at = {}) {
    if (!(pressedMask | releasedMask) || count == kMaxPending) return;
    queue[(head + count++) % kMaxPending] = {pressedMask, releasedMask, at};
  }

  // Edges for one frame; `at` gets the sample of the last edge handed out. False when none.
  bool take(uint8_t& pressedMask, uint8_t& releasedMask, ButtonSample* at = nullptr) {
    pressedMask = releasedMask = 0;
    uint8_t buttons = 0;
    for (; count; --count, head = (head + 1) % kMaxPending) {
      const Edge& edge = queue[head];
      const uint8_t mine = edge.pressed | edge.released;
      // Stop at another button, a second press, or a second release.
      if (buttons &&
          ((mine & ~buttons) | (edge.pressed & (pressedMask | releasedMask)) | (edge.released & releasedMask)))
        break;
      pressedMask |= edge.pressed;
      releasedMask |= edge.released;
      buttons |= mine;
      if (at) *at = edge.at;
    }
    return buttons != 0;
  }

  bool pending() const { return count != 0; }
};

// Everything the sample timer hands the main loop: the edges it collected and
// its latest sample. Plain data: the owner serialises every call.
//
// A pass of the main loop takes its edges and a copy of the sample of its last edge
// (the latest sample when there is none) in one step, and answers levels and held
// times from that copy until the next pass. Read live instead, a release the timer
// sees after the pass took its edges shows up as "not held" with no release edge
// yet: code that waits for a button to be let go (the swallowed release after a hold
// to wake) takes that pass, and the release then arrives on the next pass as a fresh
// short press.
struct ButtonFrames {
  static constexpr uint8_t kPowerButton = 6;
  ButtonEdgeLatch edges;
  ButtonSample latest;
  ButtonSample frame;
  uint8_t framePressed = 0;
  uint8_t frameReleased = 0;

  // Timer side, after each debounce step.
  void publish(const uint8_t pressedMask, const uint8_t releasedMask, const ButtonSample& now) {
    edges.add(pressedMask, releasedMask, now);
    latest = now;
  }
  // Main loop, once per pass: the edges and the levels this pass handles, the levels as they
  // stood at its last edge. With no edge left, the latest sample.
  void beginFrame() {
    if (!edges.take(framePressed, frameReleased, &frame)) frame = latest;
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
