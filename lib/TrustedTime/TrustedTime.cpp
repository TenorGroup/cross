#include "TrustedTime.h"

#include <Arduino.h>
#include <HalClock.h>
#include <Logging.h>
#include <Preferences.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace trustedtime {

namespace {

// Below this the clock was obviously never set (2025-01-01 UTC).
constexpr int64_t MIN_VALID_EPOCH = 1735689600LL;
// Above this a value is a fault, not a date (2100-01-01 UTC). Every supported
// RTC chip stops at 2099, so anything past this is a dead battery or a corrupt read --
// and the floor never lowers again, so adopting one would lock every real clock
// out from then on.
constexpr int64_t MAX_VALID_EPOCH = 4102444800LL;
// How far ahead of the persisted floor an RTC may read. A chip whose calendar is
// a year past what we have already trusted is faulty rather than right, and the
// days either side cover a long power-off: past this the clock stays on the
// floor until SNTP answers.
constexpr int64_t MAX_RTC_LEAD_SECS = 31536000LL;
// With no floor on record yet (a first boot, a wiped NVS) the firmware's own build date stands in for it: an RTC more
// than ten years past the build is a faulty chip, and year 2099 must not become a floor nothing can lower (D12).
constexpr int64_t MAX_BUILD_LEAD_SECS = 10LL * 31536000LL;

// Epoch of the build date, from the compiler's "Mmm dd yyyy".
constexpr int64_t buildEpoch() {
  constexpr const char* date = __DATE__;
  constexpr const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int month = 0;
  for (int i = 0; i < 12; ++i) {
    if (date[0] == months[i * 3] && date[1] == months[i * 3 + 1] && date[2] == months[i * 3 + 2]) month = i + 1;
  }
  const int day = (date[4] == ' ' ? 0 : date[4] - '0') * 10 + (date[5] - '0');
  const int year = (date[7] - '0') * 1000 + (date[8] - '0') * 100 + (date[9] - '0') * 10 + (date[10] - '0');
  // Days from civil (H. Hinnant): the days between 1970-01-01 and the date.
  const int y = month <= 2 ? year - 1 : year;
  const int era = y / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (static_cast<int64_t>(era) * 146097 + doe - 719468) * 86400;
}
// NVS wear guard: only rewrite the floor when it moved by at least this much.
constexpr int64_t MIN_ADVANCE_SECS = 60;

constexpr const char* PREFS_NAMESPACE = "cptime";
constexpr const char* PREFS_KEY = "floor";

// Latest time this boot has seen (or restored from NVS). trustedNow() never
// reports earlier, so a backward clock step (a bad SNTP answer, a manual set)
// cannot reopen an expired loan. Updated from the SNTP callback's lwIP task as
// well as the main task; 64-bit atomics are not lock-free on the C3.
int64_t ramFloor = 0;
portMUX_TYPE ramFloorLock = portMUX_INITIALIZER_UNLOCKED;

// Raises the in-RAM floor to `value` if higher; returns the resulting floor.
int64_t raiseFloor(const int64_t value) {
  portENTER_CRITICAL(&ramFloorLock);
  if (value > ramFloor) ramFloor = value;
  const int64_t floor = ramFloor;
  portEXIT_CRITICAL(&ramFloorLock);
  return floor;
}

int64_t readFloor() {
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, /*readOnly=*/true)) return 0;
  const int64_t value = prefs.getLong64(PREFS_KEY, 0);
  prefs.end();
  return value;
}

void writeFloor(const int64_t value) {
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, /*readOnly=*/false)) return;
  prefs.putLong64(PREFS_KEY, value);
  prefs.end();
}

// True while the system clock is only the NVS floor restored at boot (no RTC, or an RTC behind the floor) and
// nothing real has set it since: it can lag the wall clock by the whole time the device sat powered off.
bool floorOnly = false;

}  // namespace

void init() {
  const int64_t floor = readFloor();
  if (floor < MIN_VALID_EPOCH) return;
  raiseFloor(floor);
  if (static_cast<int64_t>(time(nullptr)) < floor) {
    // Cold boot reset the clock; resume from the floor so time keeps moving
    // forward across power cycles instead of restarting at epoch 0.
    timeval tv = {static_cast<time_t>(floor), 0};
    settimeofday(&tv, nullptr);
    floorOnly = true;
    LOG_DBG("TIME", "Clock restored to persisted floor");
  }
}

void note() {
  const int64_t now = static_cast<int64_t>(time(nullptr));
  if (now < MIN_VALID_EPOCH) return;
  raiseFloor(now);
  if (now - readFloor() >= MIN_ADVANCE_SECS) writeFloor(now);
}

void adopt(const int64_t epoch) {
  // Out of range: leave the clock and the floor exactly as they are.
  if (epoch < MIN_VALID_EPOCH || epoch > MAX_VALID_EPOCH) return;
  // Compared against the persisted floor, not the in-RAM one: boot calls this
  // before init(), so the in-RAM floor is still empty and the persisted value is
  // the only record of a time this device has already lived through.
  const int64_t stored = readFloor();
  const int64_t ceiling = stored >= MIN_VALID_EPOCH ? stored + MAX_RTC_LEAD_SECS : buildEpoch() + MAX_BUILD_LEAD_SECS;
  if (epoch > ceiling) return;
  // Deep sleep keeps the system clock, so a wake arrives with a live clock and
  // an empty floor while the RTC may be the older reading: whichever is newer
  // wins, and only inside the supported window.
  const int64_t now = static_cast<int64_t>(time(nullptr));
  const bool nowPlausible = now >= MIN_VALID_EPOCH && now <= MAX_VALID_EPOCH;
  const int64_t candidate = nowPlausible && now > epoch ? now : epoch;
  if (candidate < raiseFloor(0)) return;  // never rewind below a time we already trust
  raiseFloor(candidate);
  timeval tv = {static_cast<time_t>(candidate), 0};
  settimeofday(&tv, nullptr);
  floorOnly = false;
  if (candidate - readFloor() >= MIN_ADVANCE_SECS) writeFloor(candidate);
}

// HalClock is the only SNTP owner; this module never configures SNTP itself. A sync it ran is noted here so the
// floor follows it.
void noteNetworkSync() {
  floorOnly = false;
  note();
}

bool clockNeedsSync() { return floorOnly; }

void startSync() {
  // Only a clock that is just the restored floor needs the network; any other clock is already real time and the
  // floor merely catches up with it.
  if (floorOnly && halClock.syncFromNTP()) {
    noteNetworkSync();
    return;
  }
  note();
}

bool syncNow(const uint32_t timeoutMs) {
  // HalClock bounds its own wait (about 5 s); the argument is kept for callers that name their own limit.
  (void)timeoutMs;
  if (!halClock.syncFromNTP()) return false;
  noteNetworkSync();
  return true;
}

int64_t trustedNow() {
  const int64_t now = static_cast<int64_t>(time(nullptr));
  // Never earlier than a time already seen; 0 while neither is trustworthy.
  const int64_t floor = raiseFloor(now >= MIN_VALID_EPOCH ? now : 0);
  return floor >= MIN_VALID_EPOCH ? floor : 0;
}

}  // namespace trustedtime
