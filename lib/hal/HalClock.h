#pragma once

#include <Arduino.h>
#include <Rtc.h>

class HalClock;
extern HalClock halClock;  // Singleton

class HalClock {
  bool _available = false;
  mutable Rtc _sdkRtc;
  // The RTC keeps UTC; local time comes from newlib's localtime_r under the
  // POSIX TZ rule set via setTimezone(), so zones with DST are correct
  // year-round. Cached as a UTC epoch to keep the RTC bus quiet.
  mutable time_t _cachedUtc = 0;
  mutable bool _hasCachedTime = false;
  mutable unsigned long _lastPollMs = 0;

  static constexpr unsigned long CLOCK_POLL_MS = 10000;  // 10 seconds

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  // True if an RTC is present on this device
  bool isAvailable() const { return _available; }

  // True when the running system clock has a supported UTC calendar date.
  bool hasValidTime() const;

  // Set the POSIX TZ rule (e.g. "CET-1CEST,M3.5.0,M10.5.0/3") applied to every
  // read. nullptr/empty falls back to UTC. Drops the read cache so the change
  // shows immediately. Tenor: driven by timezones::applyToClock() (called at
  // boot and whenever clockTimezone/clockDst/clockAutoTimezone change).
  void setTimezone(const char* posixTz);

  // Current wall-clock time in the configured timezone.
  // Returns false if RTC is not available.
  bool localTime(struct tm& out) const;

  // Get current local hour (0-23) and minute (0-59).
  // Returns false until the system clock has valid time from NTP or the RTC.
  bool getTime(uint8_t& hour, uint8_t& minute) const;

  // UTC date and time from the same system clock used by HTTPS. RTC boot
  // seeding and NTP populate it; local date conversion belongs to the caller.
  // Returns false after a full power loss until a valid time source is available.
  bool getDateTime(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute) const;

  // Format the local time (configured timezone) into a caller-provided buffer.
  // 24h mode produces "HH:MM" (needs >=6 bytes); 12h mode produces "H:MM AM"/"HH:MM PM" (needs >=9 bytes).
  // use12Hour: when true, format as 12-hour clock with AM/PM suffix.
  // Returns false until the system clock has valid time from NTP or the RTC.
  bool formatTime(char* buf, size_t bufSize, bool use12Hour = false) const;

  // Sync system time from NTP, then update the external RTC when available.
  // Requires WiFi to be connected. An absent or failed RTC does not block NTP.
  // Blocks for up to ~5s while waiting for SNTP response.
  // Returns true once SNTP establishes a usable system epoch for HTTPS.
  //
  // Debouncing (skip if already synced once) is enforced by the caller, not here,
  // so the HAL stays free of any app-layer settings dependency.
  bool syncFromNTP();
};
