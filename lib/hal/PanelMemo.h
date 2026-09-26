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

//
// The three VER bytes the X3 probe read ride along for the Settings row that names the panel chip
// (a wake does not probe again). They count only next to a valid controller word: their own check
// folds that word in, so VER from another memo or from garbage never reads as VER.
struct Memo {
  uint32_t word;
  uint32_t check;
  uint32_t ver;       // VER_TAG and VER bytes 0-2, or 0 when the probe read none
  uint32_t verCheck;  // ~(ver ^ word)
};

constexpr uint32_t TAG = 0x9A4E0000u;
constexpr uint32_t VER_TAG = 0x56000000u;

inline Memo make(const uint8_t controller, const uint8_t variant, const uint8_t* ver = nullptr) {
  const uint32_t word = TAG | static_cast<uint32_t>(controller) << 8 | variant;
  const uint32_t verWord =
      ver ? VER_TAG | static_cast<uint32_t>(ver[0]) << 16 | static_cast<uint32_t>(ver[1]) << 8 | ver[2] : 0u;
  return {word, ~word, verWord, ~(verWord ^ word)};
}

// True with the remembered controller and variant; false when this boot must probe.
inline bool read(const Memo& memo, const bool deepSleepWake, uint8_t& controller, uint8_t& variant) {
  if (!deepSleepWake || memo.check != ~memo.word || (memo.word & 0xFFFF0000u) != TAG) return false;
  controller = static_cast<uint8_t>(memo.word >> 8);
  variant = static_cast<uint8_t>(memo.word);
  return true;
}

// True with the three VER bytes kept next to a controller read() accepts.
inline bool readVer(const Memo& memo, const bool deepSleepWake, uint8_t ver[3]) {
  uint8_t controller = 0, variant = 0;
  if (!read(memo, deepSleepWake, controller, variant) || memo.verCheck != ~(memo.ver ^ memo.word) ||
      (memo.ver & 0xFF000000u) != VER_TAG) {
    return false;
  }
  ver[0] = static_cast<uint8_t>(memo.ver >> 16);
  ver[1] = static_cast<uint8_t>(memo.ver >> 8);
  ver[2] = static_cast<uint8_t>(memo.ver);
  return true;
}

}  // namespace panelmemo
