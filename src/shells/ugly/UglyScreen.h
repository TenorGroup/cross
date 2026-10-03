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
  enum class Key : uint8_t { Up, Down, Left, Right, Confirm, Back, UpHold, DownHold };
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
  // The hold threshold of Up and Down, in ms.
  static constexpr unsigned long HOLD_MS = 700;

 private:
  static constexpr uint8_t QUEUE = 8;
  Key queue[QUEUE];
  uint8_t head = 0, count = 0;
  std::function<void()> later;
  void push(Key key);
};

}  // namespace ugly
