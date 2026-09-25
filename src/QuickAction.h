#pragma once

#include <cstdint>

#include "CrossPointSettings.h"

// The short actions a short power press or a hard shake runs from the main loop,
// on whatever screen is in front. One decision for both: what the configured
// action means here, or nothing.
namespace quickaction {

enum class Trigger : uint8_t { PowerRelease, Shake };
enum class Outcome : uint8_t { None, Refresh, Sleep, PageForward };

// The power button's action (a SHORT_PWRBTN value) behind a SHAKE_ACTION value.
inline uint8_t shakeAsPowerAction(const uint8_t shakeAction) {
  switch (shakeAction) {
    case CrossPointSettings::SHAKE_REFRESH:
      return CrossPointSettings::FORCE_REFRESH;
    case CrossPointSettings::SHAKE_SLEEP:
      return CrossPointSettings::SLEEP;
    case CrossPointSettings::SHAKE_PAGE_TURN:
      return CrossPointSettings::PAGE_TURN;
    default:
      return CrossPointSettings::IGNORE;
  }
}

// `action` is a SHORT_PWRBTN value. A page turn means something only in a book,
// so elsewhere it does nothing. For the button, Sleep fires on the press itself
// (getPowerButtonDuration) and Page Turn, Footnotes and Confirm are read by the
// reader and the input map, so its release only ever refreshes from here.
inline Outcome resolve(const uint8_t action, const Trigger trigger, const bool foregroundReader) {
  switch (action) {
    case CrossPointSettings::FORCE_REFRESH:
      return Outcome::Refresh;
    case CrossPointSettings::SLEEP:
      return trigger == Trigger::Shake ? Outcome::Sleep : Outcome::None;
    case CrossPointSettings::PAGE_TURN:
      return trigger == Trigger::Shake && foregroundReader ? Outcome::PageForward : Outcome::None;
    default:
      return Outcome::None;
  }
}

}  // namespace quickaction
