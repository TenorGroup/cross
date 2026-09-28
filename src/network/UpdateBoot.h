#pragma once
#include <cstdint>

// The restart into the update screen. Wi-Fi alone takes ~58 KB and a TLS record ~17 KB in one
// piece, which a session that has been to Home and a book rarely has left, so the install runs in
// a boot of its own: no Home, no SD font catalog, nothing but the update screen (main.cpp).
//
// Two things arm it: the user choosing Update on the update screen, and the probe's dry-run
// command. setup() takes it first thing on every boot, so it is spent before anything can crash:
// a crash, a power cut or a hang during the update boots normally the next time.
namespace update_boot {

constexpr uint32_t MAGIC = 0x07A5B007;
constexpr uint8_t MAX_DRY_RUNS = 20;

// Lives in RTC_NOINIT memory: kept across ESP.restart(), garbage after a power loss.
struct Slot {
  uint32_t magic;
  uint32_t dryRunsLeft;   // 0: the confirmed install; else dry runs still to do, this boot's included
  uint32_t dryRunsTotal;  // the series length, for the run numbers in the probe's lines
  uint32_t check;
};

struct Request {
  bool armed = false;
  uint8_t dryRunsLeft = 0;
  uint8_t dryRunsTotal = 0;
  bool dryRun() const { return dryRunsLeft > 0; }
};

inline uint32_t checkOf(const Slot& slot) {
  return ~(slot.magic ^ (slot.dryRunsLeft << 8) ^ (slot.dryRunsTotal << 16));
}

inline void arm(Slot& slot, const uint8_t dryRunsLeft = 0, const uint8_t dryRunsTotal = 0) {
  slot.magic = MAGIC;
  slot.dryRunsLeft = dryRunsLeft;
  slot.dryRunsTotal = dryRunsTotal;
  slot.check = checkOf(slot);
}

// Read and clear: one armed restart is one update boot.
inline Request take(Slot& slot) {
  Request request;
  const bool valid = slot.magic == MAGIC && slot.check == checkOf(slot) && slot.dryRunsLeft <= slot.dryRunsTotal &&
                     slot.dryRunsTotal <= MAX_DRY_RUNS;
  if (valid) {
    request.armed = true;
    request.dryRunsLeft = static_cast<uint8_t>(slot.dryRunsLeft);
    request.dryRunsTotal = static_cast<uint8_t>(slot.dryRunsTotal);
  }
  slot = Slot{};
  return request;
}

}  // namespace update_boot
