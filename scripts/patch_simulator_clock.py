"""Keep the pinned simulator clock facade aligned with the system-clock HAL.

The firmware HalClock (lib/hal/HalClock.h) reads one system clock: dates come
back in UTC, local time follows the POSIX TZ rule set by setTimezone(), and a
clock counts as valid from 2025 on. The simulator's clock is replaced with the
same contract so reading statistics and the status bar behave as on the device.
These changes affect the simulator dependency only.
"""
from pathlib import Path

Import("env")
root = Path(env["PROJECT_LIBDEPS_DIR"]) / env["PIOENV"] / "simulator" / "src"

HEADER = """#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <ctime>

class HalClock;
extern HalClock halClock;

class HalClock {
  bool _available = false;

 public:
  void begin();
  bool isAvailable() const { return _available; }
  bool hasValidTime() const;
  void setTimezone(const char *posixTz);
  bool localTime(struct tm &out) const;
  bool getTime(uint8_t &hour, uint8_t &minute) const;
  bool getDateTime(uint16_t &year, uint8_t &month, uint8_t &day, uint8_t &hour, uint8_t &minute) const;
  bool utcOffsetMinutes(int &minutes) const;
  bool formatTime(char *buf, size_t bufSize, bool use12Hour = false) const;
  bool syncFromNTP();
};
"""

SOURCE = """#include "HalClock.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

HalClock halClock;

namespace {
bool readUtc(std::tm &utc) {
  const std::time_t now = std::time(nullptr);
  if (now < 1735689600) return false;
#if defined(_WIN32)
  if (gmtime_s(&utc, &now) != 0) return false;
#else
  if (!gmtime_r(&now, &utc)) return false;
#endif
  return utc.tm_year <= 199;
}
}  // namespace

void HalClock::begin() {
#if defined(SIMULATOR_DEVICE_X3) || defined(SIMULATOR_DEVICE_X4_PRO) || defined(SIMULATOR_DEVICE_X4_CLASSIC) || \\
    defined(SIMULATOR_DEVICE_STICKY) || defined(SIMULATOR_DEVICE_PAPERMONO)
  _available = true;
#else
  _available = false;
#endif
}

bool HalClock::hasValidTime() const {
  std::tm utc{};
  return readUtc(utc);
}

void HalClock::setTimezone(const char *posixTz) {
  setenv("TZ", posixTz && posixTz[0] != '\\0' ? posixTz : "UTC0", 1);
  tzset();
}

bool HalClock::localTime(struct tm &out) const {
  if (!hasValidTime()) return false;
  const std::time_t now = std::time(nullptr);
#if defined(_WIN32)
  return localtime_s(&out, &now) == 0;
#else
  return localtime_r(&now, &out) != nullptr;
#endif
}

bool HalClock::getTime(uint8_t &hour, uint8_t &minute) const {
  std::tm local{};
  if (!localTime(local)) return false;
  hour = static_cast<uint8_t>(local.tm_hour);
  minute = static_cast<uint8_t>(local.tm_min);
  return true;
}

bool HalClock::getDateTime(uint16_t &year, uint8_t &month, uint8_t &day, uint8_t &hour, uint8_t &minute) const {
  std::tm utc{};
  if (!readUtc(utc)) return false;
  year = static_cast<uint16_t>(utc.tm_year + 1900);
  month = static_cast<uint8_t>(utc.tm_mon + 1);
  day = static_cast<uint8_t>(utc.tm_mday);
  hour = static_cast<uint8_t>(utc.tm_hour);
  minute = static_cast<uint8_t>(utc.tm_min);
  return true;
}

bool HalClock::utcOffsetMinutes(int &minutes) const {
  std::tm utc{};
  std::tm local{};
  if (!readUtc(utc) || !localTime(local)) return false;
  int days = local.tm_yday - utc.tm_yday;
  if (days > 1) days = -1;
  if (days < -1) days = 1;
  minutes = days * 1440 + (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);
  return true;
}

bool HalClock::formatTime(char *buf, size_t bufSize, bool use12Hour) const {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;
  std::tm local{};
  if (!localTime(local)) return false;
  if (use12Hour) {
    const bool pm = local.tm_hour >= 12;
    int hour12 = local.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    std::snprintf(buf, bufSize, "%d:%02d %s", hour12, local.tm_min, pm ? "PM" : "AM");
  } else {
    std::snprintf(buf, bufSize, "%02d:%02d", local.tm_hour, local.tm_min);
  }
  return true;
}

bool HalClock::syncFromNTP() { return hasValidTime(); }
"""

for name, text in (("HalClock.h", HEADER), ("HalClock.cpp", SOURCE)):
    path = root / name
    if not path.exists():
        raise RuntimeError("Simulator clock source missing; review patch before building")
    if path.read_text() != text:
        path.write_text(text)
