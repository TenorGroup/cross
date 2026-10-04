#include "UglyScreen.h"

#include "MappedInputManager.h"
#include "activities/RenderLock.h"

namespace ugly {

void Screen::push(const Key key) {
  if (count < QUEUE) queue[(head + count++) % QUEUE] = key;
}

void Screen::loop() {
  using Button = MappedInputManager::Button;
  // As in tenor/cross: the two front buttons (Left, Right) walk up and down, the two edge buttons (Up, Down) go
  // sideways, to the page next door. A hold on a front button is reported once, while it is down, and eats the
  // release that follows.
  if (mappedInput.wasLongPressed(Button::Left, HOLD_MS)) push(Key::UpHold);
  else if (mappedInput.wasReleased(Button::Left)) push(Key::Up);
  if (mappedInput.wasLongPressed(Button::Right, HOLD_MS)) push(Key::DownHold);
  else if (mappedInput.wasReleased(Button::Right)) push(Key::Down);
  if (mappedInput.wasReleased(Button::Up)) push(Key::Left);
  if (mappedInput.wasReleased(Button::Down)) push(Key::Right);
  if (mappedInput.wasReleased(Button::Confirm)) push(Key::Confirm);
  if (mappedInput.wasReleased(Button::Back)) push(Key::Back);
  if (count == 0) return;

  bool changed = false;
  {
    // Never wait for the panel: the keys stay queued until a pass finds the lock free.
    RenderLock lock(RenderLock::TryTake{});
    if (!lock.acquired()) return;
    while (count > 0 && !later) {
      const Key key = queue[head];
      head = static_cast<uint8_t>((head + 1) % QUEUE);
      --count;
      changed |= onKey(key);
    }
  }
  if (later) {
    count = 0;  // the screen is going away; nothing queued belongs to the next one
    auto next = std::move(later);
    later = nullptr;
    next();
    return;
  }
  afterKeys();
  if (changed) requestUpdate();
}

}  // namespace ugly
