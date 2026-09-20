#include <HalClock.h>
#include <Rtc.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include <ArduinoJson.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "CrossPointSettings.h"
#include "ReadingStatsStore.h"
#include "util/ReadingHabits.h"

namespace fake {
bool rtcPresent = false;
bool rtcReadable = false;
bool wifiConnected = true;
bool ntpResponds = true;
time_t epoch = 0;
time_t ntpEpoch = 1789819200;  // 2026-09-19 12:00 UTC.
unsigned long milliseconds = 1;
Rtc::DateTime stored;

void resetClock() {
  rtcPresent = false;
  rtcReadable = false;
  wifiConnected = true;
  ntpResponds = true;
  epoch = 0;
  ntpEpoch = 1789819200;
  milliseconds = 1;
  stored = {};
  SETTINGS = {};
  halClock = HalClock{};
}
}  // namespace fake

unsigned long millis() { return fake::milliseconds; }
void delay(const unsigned long ms) { fake::milliseconds += ms; }
void configTzTime(const char*, const char*, const char*) {}
int sntp_get_sync_status() {
  if (!fake::ntpResponds) return 0;
  fake::epoch = fake::ntpEpoch;
  return SNTP_SYNC_STATUS_COMPLETED;
}
extern "C" time_t time(time_t* output) {
  if (output) *output = fake::epoch;
  return fake::epoch;
}
extern "C" int settimeofday(const timeval* value, const struct timezone*) {
  fake::epoch = value->tv_sec;
  return 0;
}
bool Rtc::begin() { return fake::rtcPresent; }
bool Rtc::now(DateTime& value) {
  value = fake::stored;
  return fake::rtcReadable;
}
bool Rtc::set(const DateTime& value) {
  fake::stored = value;
  return true;
}
WiFiClass WiFi;
int WiFiClass::status() const { return fake::wifiConnected ? WL_CONNECTED : 0; }
void testLog(const char*, const char*, ...) {}

namespace {
int failures = 0;
int scenarios = 0;

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      ++failures;                                                                                                      \
      std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #condition);                                                \
    }                                                                                                                  \
  } while (0)

JsonArray dayRow(JsonDocument& doc, const size_t index) { return doc["ngay"][index].as<JsonArray>(); }

void noRtcNtpIgnoresHistoricalFlag() {
  ++scenarios;
  fake::resetClock();
  SETTINGS.clockHasBeenSynced = 0;
  SETTINGS.clockUtcOffsetQ = 76;  // UTC+7.
  halClock.begin();
  CHECK(!halClock.isAvailable());
  CHECK(halClock.syncFromNTP());
  CHECK(ReadingStatsStore::currentDay() == 20260919u);

  const auto stamp = ReadingStatsStore::habitStamp();
  const auto utcDay = habits::ordinal(2026, 9, 19);
  CHECK(stamp.day == utcDay);
  CHECK(stamp.utcMinute == (utcDay - 1) * 1440 + 12 * 60);
  CHECK(stamp.minute == 19 * 60);
  CHECK(stamp.offset == 420);
}

void invalidClockStaysUndated() {
  ++scenarios;
  fake::resetClock();
  halClock.begin();
  CHECK(ReadingStatsStore::currentDay() == 0);
  CHECK(ReadingStatsStore::habitStamp().day == 0);

  fake::epoch = 4102444800LL;  // 2100-01-01 UTC, outside supported calendar range.
  CHECK(ReadingStatsStore::currentDay() == 0);
  CHECK(ReadingStatsStore::habitStamp().day == 0);

  fake::epoch = 0;
  fake::rtcPresent = fake::rtcReadable = true;
  fake::stored = {2026, 2, 31, 12, 0, 0, 0};
  halClock = HalClock{};
  halClock.begin();
  CHECK(ReadingStatsStore::currentDay() == 0);
}

void offsetMovesBothStatisticsClocksToNextDay() {
  ++scenarios;
  fake::resetClock();
  SETTINGS.clockHasBeenSynced = 0;
  SETTINGS.clockUtcOffsetQ = 76;
  fake::epoch = 1789848000;  // 2026-09-19 20:00 UTC, 2026-09-20 03:00 UTC+7.
  halClock.begin();

  CHECK(ReadingStatsStore::currentDay() == 20260920u);
  const auto stamp = ReadingStatsStore::habitStamp();
  CHECK(stamp.day == habits::ordinal(2026, 9, 20));
  CHECK(stamp.minute == 3 * 60);
  CHECK(stamp.offset == 420);
}

void persistedOldAndUndatedStatisticsStayIntact() {
  ++scenarios;
  fake::resetClock();
  SETTINGS.clockHasBeenSynced = 0;
  SETTINGS.clockUtcOffsetQ = 76;

  JsonDocument persisted;
  persisted["schema"] = 3;
  auto rows = persisted["ngay"].to<JsonArray>();
  auto first = rows.add<JsonArray>();
  first.add(20260917u);
  first.add(2u);
  first.add(3u);
  first.add(4000u);
  auto second = rows.add<JsonArray>();
  second.add(20260918u);
  second.add(4u);
  second.add(5u);
  second.add(5000u);
  persisted["lacPhut"] = 12u;
  persisted["lacTrang"] = 34u;
  persisted["lacMs"] = 5678u;

  auto& store = READING_STATS;
  CHECK(store.fromJson(persisted.as<JsonVariantConst>()));
  store.record(ReadingStatsStore::currentDay(), 54322, 2, 10);
  CHECK(store.kho.phutChuaBietNgay() == 13u);
  CHECK(store.kho.trangChuaBietNgay() == 36u);
  CHECK(store.kho.msChuaBietNgay() == 0u);

  halClock.begin();
  CHECK(halClock.syncFromNTP());
  const uint32_t today = ReadingStatsStore::currentDay();
  CHECK(today == 20260919u);
  store.record(today, 62000, 3, 11);
  store.observeHabits(1000, 1, 1000);
  CHECK(!store.habitLedger.clockLost);

  JsonDocument output;
  store.toJson(output);
  CHECK(output["lacPhut"].as<uint32_t>() == 13u);
  CHECK(output["lacTrang"].as<uint32_t>() == 36u);
  CHECK(!output["lacMs"]);
  CHECK(output["ngay"].size() == 3u);
  CHECK(dayRow(output, 0)[0].as<uint32_t>() == 20260917u);
  CHECK(dayRow(output, 0)[1].as<uint16_t>() == 2u);
  CHECK(dayRow(output, 0)[2].as<uint16_t>() == 3u);
  CHECK(dayRow(output, 0)[3].as<uint16_t>() == 4000u);
  CHECK(dayRow(output, 1)[0].as<uint32_t>() == 20260918u);
  CHECK(dayRow(output, 1)[1].as<uint16_t>() == 4u);
  CHECK(dayRow(output, 1)[2].as<uint16_t>() == 5u);
  CHECK(dayRow(output, 1)[3].as<uint16_t>() == 5000u);
  CHECK(dayRow(output, 2)[0].as<uint32_t>() == 20260919u);
  CHECK(dayRow(output, 2)[1].as<uint16_t>() == 1u);
  CHECK(dayRow(output, 2)[2].as<uint16_t>() == 3u);
  CHECK(dayRow(output, 2)[3].as<uint16_t>() == 2000u);
}
}  // namespace

int main() {
  noRtcNtpIgnoresHistoricalFlag();
  invalidClockStaysUndated();
  offsetMovesBothStatisticsClocksToNextDay();
  persistedOldAndUndatedStatisticsStayIntact();
  std::printf("%d scenarios, %d failures\n", scenarios, failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
