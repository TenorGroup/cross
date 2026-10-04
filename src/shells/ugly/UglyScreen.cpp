#include "UglyScreen.h"

#include "MappedInputManager.h"
#include "activities/RenderLock.h"

namespace ugly {

void Screen::push(const Key key) {
  if (count < QUEUE) queue[(head + count++) % QUEUE] = key;
}

#if FREEINK_DEVICE_X4PRO
void Screen::push(const Key key, const int x, const int y) {
  if (count >= QUEUE) return;
  const int at = (head + count) % QUEUE;
  queueX[at] = static_cast<int16_t>(x);
  queueY[at] = static_cast<int16_t>(y);
  push(key);
}

// The touch screen speaks through the scribble recognizer alone (it already reads the contact the SDK
// reads), so a slanted first stroke of an X never turns a page as a swipe. The SDK gives the hold, which
// ends the contact, and the Home key, which is Back.
void Screen::readTouch() {
  if (mappedInput.wasHomeGesture()) push(Key::Back);
  int x = 0, y = 0;
  if (mappedInput.wasScreenLongPress(x, y)) {
    push(Key::Hold, x, y);
    swallowStroke = true;
  }
  if (const scribble::Stroke* s = mappedInput.endedStroke()) {
    if (!inkFresh) inkCount = 0;
    inkFresh = true;
    Ink& k = ink[inkCount < 2 ? inkCount++ : 1];
    const int every = (s->n + Ink::POINTS - 1) / Ink::POINTS;
    k.n = 0;
    for (int i = 0; i < s->n && k.n < Ink::POINTS; i += every, ++k.n) {
      k.x[k.n] = s->p[i].x;
      k.y[k.n] = s->p[i].y;
    }
  }
  scribble::Result r;
  if (!mappedInput.wasScribble(r)) return;
  inkFresh = false;
  if (swallowStroke) {
    swallowStroke = false;
    return;
  }
  using scribble::Kind;
  // A shaky tap, too big for the recognizer's tap and too small for anything else, is still a tap.
  const bool small = r.strokes == 1 && scribble::detail::diagOf(r.box) < scribble::MIN_SIZE_PX;
  switch (r.kind) {
    case Kind::Tap:
      push(Key::Tap, r.x, r.y);
      break;
    case Kind::Swipe: {
      const int dx = r.to.x - r.from.x, dy = r.to.y - r.from.y;
      const bool across = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
      push(across ? (dx < 0 ? Key::SwipeLeft : Key::SwipeRight) : (dy < 0 ? Key::SwipeUp : Key::SwipeDown), r.from.x, r.from.y);
      break;
    }
    case Kind::Cross:
      push(Key::Cross, r.x, r.y);
      break;
    case Kind::Circle:
      push(Key::Ring, r.x, r.y);
      break;
    case Kind::Unknown:
      if (small)
        push(Key::Tap, r.from.x, r.from.y);
      else
        push(Key::Scrawl, r.x, r.y);
      break;
    default:
      break;
  }
}
#endif

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
#if FREEINK_DEVICE_X4PRO
  // A swipe from the left edge is the SDK's Back; here every swipe across turns the page, and Back is the Home key.
  if (mappedInput.wasReleased(Button::Back) && !mappedInput.wasBackGesture()) push(Key::Back);
  readTouch();
#else
  if (mappedInput.wasReleased(Button::Back)) push(Key::Back);
#endif
  if (count == 0) return;

  bool changed = false;
  {
    // Never wait for the panel: the keys stay queued until a pass finds the lock free.
    RenderLock lock(RenderLock::TryTake{});
    if (!lock.acquired()) return;
    while (count > 0 && !later) {
      const Key key = queue[head];
#if FREEINK_DEVICE_X4PRO
      touchX = queueX[head];
      touchY = queueY[head];
#endif
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
