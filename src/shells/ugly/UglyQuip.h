#pragma once
// The line of abuse an action earns: open something and it mocks that thing, pick a value and it mocks the
// pick. Shown where the action lands, in the same refresh as the action: the subtitle of the notebook page,
// or the line of the sleep screen. The lines are baked from scripts/ugly/quips.csv (UglyQuips.h).
#include <cstdint>
#include <string>

struct SettingInfo;

namespace ugly {

// Must match EVENTS in scripts/ugly/gen_quips.py.
enum class Quip : uint8_t {
  OpenPage, OpenGroup, SetValue, Pin, Unpin, Delete, ShellCross, ShellUgly, Sleep, Wake, OpenBook, LeaveBook, OpenScreen
};
// Conditions with data, by event (0: none, the lines take turns). Must match WHEN in gen_quips.py.
namespace when {
inline constexpr uint8_t SLEEP_ZERO = 1, SLEEP_LOW = 2;  // nothing read today, 1 to 14 minutes
}

// What to say for `event`: `key` is a page id for OpenPage, else quipKey() of the Vietnamese words; `when`
// a condition above, falling back to the plain lines. A line with %d takes `number`, one with %s `name`.
// Empty when there is nothing to say (or the heap cannot inflate the lines this time).
std::string quip(Quip event, uint16_t key = 0, uint8_t when = 0, int number = 0, const char* name = nullptr);

// The key of a settings row and the value it holds now, in the Vietnamese words the table uses.
uint16_t valueKey(const SettingInfo& setting);

// A value changed on a tenor/cross screen while this shell is on: kept for the notebook page to say once.
void noteValue(const SettingInfo& setting);
std::string takeNoted();

}  // namespace ugly
