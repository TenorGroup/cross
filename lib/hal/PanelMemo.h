#pragma once

#include <cstdint>

// The panel controller a full boot probed, kept in RTC memory for the next wake from deep sleep.
// The controller is part of the board and cannot change while the chip sleeps, so a wake skips
// the probe (a reset pulse and a register read, about 110 ms on the X3). Only a deep-sleep wake
// trusts the memo: every other reset (power on, flash, crash, restart) probes again and writes a
// new one, so a memo can never outlive the power session it was measured in. The second word is
// the complement of the first, so whatever RTC memory holds after a power loss never reads as a
// memo.
namespace panelmemo {

struct Memo {
  uint32_t word;
  uint32_t check;
};

constexpr uint32_t TAG = 0x9A4E0000u;

inline Memo make(const uint8_t controller, const uint8_t variant) {
  const uint32_t word = TAG | static_cast<uint32_t>(controller) << 8 | variant;
  return {word, ~word};
}

// True with the remembered controller and variant; false when this boot must probe.
inline bool read(const Memo& memo, const bool deepSleepWake, uint8_t& controller, uint8_t& variant) {
  if (!deepSleepWake || memo.check != ~memo.word || (memo.word & 0xFFFF0000u) != TAG) return false;
  controller = static_cast<uint8_t>(memo.word >> 8);
  variant = static_cast<uint8_t>(memo.word);
  return true;
}

}  // namespace panelmemo
