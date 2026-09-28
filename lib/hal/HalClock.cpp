#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

HalClock halClock;  // Singleton instance

namespace {
// Keep a cold boot's seconds-since-start out of calendar and reading statistics.
constexpr time_t MIN_CLOCK_EPOCH = 1735689600;  // 2025-01-01 UTC

bool readSystemTime(struct tm& utc) {
  const time_t now = time(nullptr);
  return now >= MIN_CLOCK_EPOCH && gmtime_r(&now, &utc) && utc.tm_year <= 199;
}

bool validRtcDate(const Rtc::DateTime& dt) {
  if (dt.year < 2025 || dt.year > 2099 || dt.month < 1 || dt.month > 12 || dt.hour > 23 ||
      dt.minute > 59 || dt.second > 59) return false;
  constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const uint8_t limit = days[dt.month - 1] + (dt.month == 2 && dt.year % 4 == 0 ? 1 : 0);
  return dt.day >= 1 && dt.day <= limit;
}
}  // namespace

void HalClock::begin() {
  _available = _sdkRtc.begin();
  LOG_INF("CLK", _available ? "SDK RTC found" : "RTC not found");
  // A retained platform clock can already be newer than a failed RTC write.
  if (hasValidTime()) return;
  Rtc::DateTime dt;
  if (_available && _sdkRtc.now(dt) && validRtcDate(dt)) {
    struct tm utc = {};
    utc.tm_year = dt.year - 1900;
    utc.tm_mon = dt.month - 1;
    utc.tm_mday = dt.day;
    utc.tm_hour = dt.hour;
    utc.tm_min = dt.minute;
    utc.tm_sec = dt.second;
    setenv("TZ", "UTC0", 1);
    tzset();
    const struct timeval systemTime = {mktime(&utc), 0};
    settimeofday(&systemTime, nullptr);
  }
}

bool HalClock::hasValidTime() const {
  struct tm utc = {};
  return readSystemTime(utc);
}

bool HalClock::getDateTime(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour, uint8_t& minute) const {
  struct tm utc = {};
  if (!readSystemTime(utc)) return false;
  year = static_cast<uint16_t>(utc.tm_year + 1900);
  month = static_cast<uint8_t>(utc.tm_mon + 1);
  day = static_cast<uint8_t>(utc.tm_mday);
  hour = static_cast<uint8_t>(utc.tm_hour);
  minute = static_cast<uint8_t>(utc.tm_min);
  return true;
}

bool HalClock::utcOffsetMinutes(int& minutes) const {
  struct tm utc = {};
  if (!readSystemTime(utc)) return false;
  const time_t now = time(nullptr);
  struct tm local = {};
  localtime_r(&now, &local);
  int days = local.tm_yday - utc.tm_yday;
  if (days > 1) days = -1;  // local is still on 31 Dec while UTC reached 1 Jan
  if (days < -1) days = 1;  // local reached 1 Jan while UTC is still on 31 Dec
  minutes = days * 1440 + (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);
  return true;
}

void HalClock::setTimezone(const char* posixTz) {
  setenv("TZ", posixTz && posixTz[0] != '\0' ? posixTz : "UTC0", 1);
  tzset();
}

// The system clock is the one time source: begin() seeds it from the RTC and
// syncFromNTP() sets it, so a board without an RTC shows the time after NTP,
// and reading it keeps the RTC bus quiet.
bool HalClock::localTime(struct tm& out) const {
  struct tm utc = {};
  if (!readSystemTime(utc)) return false;
  const time_t now = time(nullptr);
  localtime_r(&now, &out);
  return true;
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  struct tm local;
  if (!localTime(local)) return false;
  hour = static_cast<uint8_t>(local.tm_hour);
  minute = static_cast<uint8_t>(local.tm_min);
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, bool use12Hour) const {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;
  struct tm local;
  if (!localTime(local)) return false;

  if (use12Hour) {
    const bool pm = local.tm_hour >= 12;
    int hour12 = local.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", hour12, local.tm_min, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", local.tm_hour, local.tm_min);
  }
  return true;
}

bool HalClock::syncFromNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  // configTzTime overwrites the process TZ with UTC0 for the SNTP exchange;
  // remember the display timezone so it can be restored below.
  const char* tzBefore = getenv("TZ");
  char savedTz[64] = {0};
  if (tzBefore) snprintf(savedTz, sizeof(savedTz), "%s", tzBefore);
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  // Wait for SNTP sync to complete (up to 5 seconds)
  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      struct tm timeinfo = {};
      // Use the same epoch floor as the HTTPS callers. SNTP success must
      // establish usable system time even on boards without an external RTC.
      if (!readSystemTime(timeinfo)) {
        LOG_ERR("CLK", "NTP completed with invalid system time");
        return false;
      }
      LOG_INF("CLK", "System time synced from NTP");
      if (!_available) {
        setTimezone(savedTz);
        return true;
      }

      Rtc::DateTime dt;
      dt.year = static_cast<uint16_t>(timeinfo.tm_year + 1900);
      dt.month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
      dt.day = static_cast<uint8_t>(timeinfo.tm_mday);
      dt.hour = static_cast<uint8_t>(timeinfo.tm_hour);
      dt.minute = static_cast<uint8_t>(timeinfo.tm_min);
      dt.second = static_cast<uint8_t>(timeinfo.tm_sec);
      dt.weekday = static_cast<uint8_t>(timeinfo.tm_wday);
      if (_sdkRtc.set(dt)) {
        LOG_INF("CLK", "RTC set to %04u-%02u-%02u %02u:%02u:%02u UTC", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                dt.second);
      } else {
        LOG_ERR("CLK", "RTC write failed; system time remains synced");
      }
      // A failed RTC write does not fail the sync: system time (used by HTTPS
      // and by readSystemTime()/getDateTime() above) is already usable.
      setTimezone(savedTz);
      return true;
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  setTimezone(savedTz);
  return false;
}
