#pragma once
// Base of the three tiers of the tenor/ugly shell. It turns button releases into keys, queues them
// while the panel is busy (a refresh holds the render lock for 390 ms, and a press that arrives
// then must not be lost), and applies them the moment the lock is free.
#include <functional>

#include "UglyInk.h"
#include "activities/Activity.h"

namespace ugly {

class Screen : public Activity {
 public:
  // Up and Down are the front buttons (up and down the screen), Left and Right the edge buttons (sideways).
  // The last nine come from a touch screen only, with the point they aim at (touchX, touchY): a tap, a hold,
  // a swipe the way the finger went, an X and a ring drawn over a row, and a scribble nobody can read.
  enum class Key : uint8_t {
    Up, Down, Left, Right, Confirm, Back, UpHold, DownHold,
    Tap, Hold, SwipeLeft, SwipeRight, SwipeUp, SwipeDown, Cross, Ring, Scrawl
  };
  void loop() final;
  // The three tiers are Home: holding Back does not throw them out to Home again.
  bool isHomeActivity() const override { return true; }

 protected:
  Screen(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity(name, renderer, mappedInput) {}
  // Runs with the render lock held: change the state, return true when the screen must be drawn again.
  virtual bool onKey(Key key) = 0;
  // Runs after a pass of keys, with the lock released and the screen staying: the place for work that reads the
  // card, which must never happen under the lock. Ask for the frame yourself when it changes something.
  virtual void afterKeys() {}
  // Leaving the screen goes through here: it runs after the lock is released, in the same pass.
  void then(std::function<void()> next) { later = std::move(next); }
  // The hold threshold of the front buttons, in ms.
  static constexpr unsigned long HOLD_MS = 700;
#if FREEINK_DEVICE_X4PRO
  // Where the touch key being handled aims, in logical px.
  int touchX = 0, touchY = 0;
  // The strokes of the last scribble, kept to be drawn back once with what it did (at most two, every
  // other sample of a long one).
  struct Ink {
    static constexpr int POINTS = 64;
    int16_t x[POINTS], y[POINTS];
    uint8_t n = 0;
  };
  Ink ink[2];
  uint8_t inkCount = 0;
  bool inkFresh = false;  // a stroke ended since the last scribble was decided
#endif

 private:
  static constexpr uint8_t QUEUE = 8;
  Key queue[QUEUE];
  uint8_t head = 0, count = 0;
  std::function<void()> later;
  void push(Key key);
#if FREEINK_DEVICE_X4PRO
  int16_t queueX[QUEUE], queueY[QUEUE];
  bool swallowStroke = false;  // a hold ended the contact: the lift that follows is no tap
  void push(Key key, int x, int y);
  void readTouch();
#endif
};

}  // namespace ugly
