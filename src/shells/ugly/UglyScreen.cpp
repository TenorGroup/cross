#include "UglyScreen.h"

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "UglyTouch.h"
#include "activities/RenderLock.h"

namespace ugly {

void Screen::push(const Key key) {
  if (count < QUEUE) queue[(head + count++) % QUEUE] = key;
}

#if FREEINK_DEVICE_X4PRO
void Screen::push(const Key key, const int x, const int y, const int toX, const int toY) {
  if (count >= QUEUE) return;
  const int at = (head + count) % QUEUE;
  queueX[at] = static_cast<int16_t>(x);
  queueY[at] = static_cast<int16_t>(y);
  queueToX[at] = static_cast<int16_t>(toX);
  queueToY[at] = static_cast<int16_t>(toY);
  push(key);
}

// The touch screen speaks through the scribble recognizer alone (it already reads the contact the SDK
// reads), so a slanted first stroke of an X never turns a page as a swipe. The SDK gives the hold, which
// ends the contact. The Home key and the swipe up from the bottom edge are ActivityManager's (to the desk).
bool Screen::markBand(const Key key) {
  if (key != Key::Strike && key != Key::Ring) return false;
  const touch::BandSpot spot = touch::bandAt(touchX, touchY);
  if (spot == touch::BandSpot::None) return false;
  const touch::Band band = touch::markBand({SETTINGS.uglyBatteryHidden != 0, SETTINGS.clockShowInHeader},
                                           key == Key::Strike ? touch::Mark::Erase : touch::Mark::Keep, spot);
  SETTINGS.uglyBatteryHidden = band.batteryHidden ? 1 : 0;
  SETTINGS.clockShowInHeader = band.clock;
  bandChanged = true;
  return true;
}

void Screen::readTouch() {
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
      push(across ? (dx < 0 ? Key::SwipeLeft : Key::SwipeRight) : (dy < 0 ? Key::SwipeUp : Key::SwipeDown), r.from.x, r.from.y,
           r.to.x, r.to.y);
      break;
    }
    case Kind::Strike:
      push(Key::Strike, r.x, r.y);
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
      touchToX = queueToX[head];
      touchToY = queueToY[head];
#endif
      head = static_cast<uint8_t>((head + 1) % QUEUE);
      --count;
#if FREEINK_DEVICE_X4PRO
      if (markBand(key)) {
        changed = true;
        continue;
      }
#endif
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
#if FREEINK_DEVICE_X4PRO
  if (bandChanged) {  // the frame first, the card after it
    bandChanged = false;
    SETTINGS.saveToFile();
  }
#endif
}

}  // namespace ugly
