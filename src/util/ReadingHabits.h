#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace habits {
constexpr size_t DAYS = 29;  // Today plus 28 completed calendar days.
constexpr size_t NAMES = 6;
enum Name : uint8_t { NIGHT, SHORT, EARLY, REGULAR, WEEKEND, LONG };
struct Stamp {
  uint32_t day = 0;  // Local ordinal, 2000-01-01 is day 1.
  uint32_t utcMinute = 0;
  uint16_t minute = 0;
  int16_t offset = 0;
};
struct Day {
  uint32_t day = 0, activeMs = 0, nightMs = 0, earlyMs = 0;
  uint16_t sessions = 0, shortSessions = 0, longSessions = 0, uncertain = 0;
  bool night = false;
};
struct Session {
  uint32_t visibleMs = 0, activeMs = 0, idleMs = 0, turns = 0;
  Stamp last{};
  bool uncertain = false, clean = false;
};
struct Summary {
  uint32_t activeMs = 0, nightMs = 0, earlyMs = 0;
  uint32_t weekdayMs = 0, weekendMs = 0;
  uint32_t sessions = 0, shortSessions = 0, longSessions = 0, uncertain = 0;
  uint8_t days = 0, recentDays = 0, nights = 0, mornings = 0, shortDays = 0, longDays = 0;
  uint8_t coverage = 0, weekends = 0, weekdayCount = 0, weekendCount = 0;
  uint8_t eligible = 0;
};
// Fixed storage, no heap or I/O. UI reads a summary; the reader only accumulates.
class Ledger {
 public:
  std::array<Day, DAYS> days{};
  Session session{};
  uint32_t firstDay = 0, lastEvaluated = 0;
  uint8_t awarded = 0, hidden = 0;
  std::array<uint8_t, NAMES> enter{}, leave{};
  bool restored = false, clockLost = false;
  uint32_t qualityGapDay = 0;
  void observe(uint32_t elapsedMs, uint16_t turns, Stamp stamp, uint32_t now);
  void settle(Stamp now);
  void finish();
  Summary summarize(uint32_t today) const;
  void evaluate(uint32_t today, bool refresh = false);
  void clear() { *this = Ledger{}; }

 private:
  uint32_t lastMonotonic = 0, clockMinute = 0, clockMonotonic = 0;
  bool clockObserved = false;
  bool hasMonotonic = false;
  Day* dayFor(uint32_t day);
};
uint32_t ordinal(uint16_t year, uint8_t month, uint8_t day);
uint32_t dateKey(uint32_t ordinalDay);
}  // namespace habits
