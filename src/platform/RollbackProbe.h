#pragma once

#include <cstdint>

// The pure part of the probe's bootloader rollback test (CMD:ROLLBACK_TEST, CMD:OTA_STATE), kept
// apart from the hardware so the host tests run it.
namespace rollback_probe {

// Crash boots still owed to the test, kept in RTC memory: it survives the panic reset and is lost
// with power. Every boot spends one, so a test never crashes more boots than it was armed for, and
// power-up garbage that happens to match the magic still fails the check word.
struct CrashCounter {
  uint32_t magic;
  uint32_t left;
  uint32_t check;
};

constexpr uint32_t kMagic = 0x524F4C42;  // "ROLB"
constexpr uint32_t kMaxCrashes = 3;

constexpr uint32_t checkWord(const uint32_t left) { return ~(kMagic ^ (left * 0x9E3779B1u)); }

inline void arm(CrashCounter& c, uint32_t crashes) {
  crashes = crashes < 1 ? 1 : (crashes > kMaxCrashes ? kMaxCrashes : crashes);
  c = {kMagic, crashes, checkWord(crashes)};
}

// Once per boot, before anything else. True when this boot is one the test crashes; the counter
// is spent before the caller crashes.
inline bool takeCrash(CrashCounter& c) {
  const bool armed = c.magic == kMagic && c.left >= 1 && c.left <= kMaxCrashes && c.check == checkWord(c.left);
  if (!armed) {
    c = {};
    return false;
  }
  --c.left;
  c.check = checkWord(c.left);
  if (c.left == 0) c = {};
  return true;
}

// One otadata entry as read from flash; crcOk is the IDF check of the sequence number.
struct OtaEntry {
  uint32_t seq;
  uint32_t state;
  bool crcOk;
};

// The entry written last: the highest sequence with a good CRC, whatever its state (a failed trial
// is marked ABORTED and keeps its sequence). -1 when otadata is blank.
inline int newestEntry(const OtaEntry (&e)[2]) {
  int newest = -1;
  for (int i = 0; i < 2; ++i) {
    if (e[i].seq == UINT32_MAX || !e[i].crcOk) continue;
    if (newest < 0 || e[i].seq > e[newest].seq) newest = i;
  }
  return newest;
}

// What the newest entry says about the bootloader, read after the reboot that follows an install
// or CMD:ROLLBACK_TEST (states from esp_ota_img_states_t).
inline const char* verdict(const uint32_t newestState) {
  switch (newestState) {
    case 4:  // ABORTED: a trial boot failed and the bootloader fell back to the previous entry
      return "rolled_back";
    case 0:  // NEW: the bootloader booted it without starting a trial
      return "none";
    case 1:  // PENDING_VERIFY: this boot is on trial and has not reached its first frame yet
      return "on_trial";
    case 2:  // VALID: a trial that passed
      return "valid";
    default:
      return "other";
  }
}

}  // namespace rollback_probe
