#pragma once

#include <I18nKeys.h>

#include <cstdint>
#include <iterator>
#include <vector>

#include "CrossPointSettings.h"

// The short actions a short power press, a hard shake, face down or face up runs from
// the main loop, on whatever screen is in front. One list of them for every setting, and
// one decision for all: what the configured action means here, or nothing.
namespace quickaction {

enum class Trigger : uint8_t { PowerRelease, Shake, FaceDown, FaceUp };
enum class Outcome : uint8_t { None, Refresh, Sleep, PageForward, Back, Confirm };

struct Choice {
  uint8_t action;  // A SHORT_PWRBTN value
  StrId label;
};

// Every short action, in the power button's stored order: its setting keeps the
// value, so a new action goes last and none moves.
inline constexpr Choice CHOICES[] = {
    {CrossPointSettings::IGNORE, StrId::STR_IGNORE},
    {CrossPointSettings::SLEEP, StrId::STR_SLEEP},
    {CrossPointSettings::PAGE_TURN, StrId::STR_PAGE_TURN},
    {CrossPointSettings::FORCE_REFRESH, StrId::STR_FORCE_REFRESH},
    {CrossPointSettings::FOOTNOTES, StrId::STR_FOOTNOTES},
    {CrossPointSettings::PWR_CONFIRM, StrId::STR_SELECT},
    {CrossPointSettings::BACK, StrId::STR_SHAKE_BACK},
};
constexpr bool choicesInValueOrder() {
  for (uint8_t i = 0; i < std::size(CHOICES); ++i) {
    if (CHOICES[i].action != i) return false;
  }
  return std::size(CHOICES) == CrossPointSettings::SHORT_PWRBTN_COUNT;
}
static_assert(choicesInValueOrder(), "one choice per short action, at its stored value");

// The shake setting's own order over the same actions, stored by place: Off first
// (the Ignore action), then the ones that make sense on any screen. Face down and face
// up list the same.
inline constexpr uint8_t SHAKE_ORDER[] = {CrossPointSettings::IGNORE,    CrossPointSettings::FORCE_REFRESH,
                                          CrossPointSettings::SLEEP,     CrossPointSettings::PAGE_TURN,
                                          CrossPointSettings::BACK,      CrossPointSettings::PWR_CONFIRM};

// The rows of the two settings, built from CHOICES.
inline std::vector<StrId> powerLabels() {
  std::vector<StrId> labels;
  for (const auto& choice : CHOICES) labels.push_back(choice.label);
  return labels;
}
inline std::vector<StrId> shakeLabels() {
  std::vector<StrId> labels;
  for (const uint8_t action : SHAKE_ORDER) {
    labels.push_back(action == CrossPointSettings::IGNORE ? StrId::STR_STATE_OFF : CHOICES[action].label);
  }
  return labels;
}

// The short action behind a shake setting value; unknown values do nothing.
inline uint8_t shakeAsPowerAction(const uint8_t shakeAction) {
  return shakeAction < std::size(SHAKE_ORDER) ? SHAKE_ORDER[shakeAction] : CrossPointSettings::IGNORE;
}

// `action` is a SHORT_PWRBTN value. The motion gestures (shake, face down, face up)
// run every action themselves. A page turn means something only in a book, so
// elsewhere it does nothing. Back and Select are pressed like the real keys, and each
// screen takes or ignores them as it does those. For the button, Sleep fires on the
// press itself (getPowerButtonDuration), Page Turn and Footnotes are read by the
// reader, and on touch boards the input map makes the release Select itself.
inline Outcome resolve(const uint8_t action, const Trigger trigger, const bool foregroundReader,
                       const bool touchPowerSelect = false) {
  const bool gesture = trigger != Trigger::PowerRelease;
  switch (action) {
    case CrossPointSettings::FORCE_REFRESH:
      return Outcome::Refresh;
    case CrossPointSettings::SLEEP:
      return gesture ? Outcome::Sleep : Outcome::None;
    case CrossPointSettings::PAGE_TURN:
      return gesture && foregroundReader ? Outcome::PageForward : Outcome::None;
    case CrossPointSettings::PWR_CONFIRM:
      return gesture || !touchPowerSelect ? Outcome::Confirm : Outcome::None;
    case CrossPointSettings::BACK:
      return Outcome::Back;
    default:
      return Outcome::None;
  }
}

}  // namespace quickaction
