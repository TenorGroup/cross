#include <HalClock.h>
#include <Logging.h>
#include <WiFi.h>
#include <HttpUrl.h>
#include <sys/time.h>
#include <time.h>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace fake {
bool rtc = false, rtcReadable = false, rtcWritable = true, wifi = true, ntpResponds = true;
time_t epoch = 0, ntpEpoch = 1789819200;
unsigned long ms = 0;
int ntpCalls = 0, rtcWrites = 0, tlsBegins = 0;
bool caConfigured = false;
Rtc::DateTime stored;
std::string logs;
void reset() {
  rtc = rtcReadable = false;
  rtcWritable = wifi = ntpResponds = true;
  epoch = 0;
  ntpEpoch = 1789819200;
  ms = 1;
  ntpCalls = rtcWrites = tlsBegins = 0;
  caConfigured = false;
  logs.clear();
  stored = Rtc::DateTime{};
  halClock = HalClock{};
}
}
unsigned long millis() { return fake::ms; }
void delay(unsigned long ms) { fake::ms += ms; }
void configTzTime(const char*, const char*, const char*) { ++fake::ntpCalls; }
int sntp_get_sync_status() {
  if (!fake::ntpResponds) return 0;
  fake::epoch = fake::ntpEpoch;
  return 1;
}
extern "C" time_t time(time_t* out) {
  if (out) *out = fake::epoch;
  return fake::epoch;
}
extern "C" int settimeofday(const timeval* tv, const struct timezone*) {
  fake::epoch = tv->tv_sec;
  return 0;
}
bool Rtc::begin() { return fake::rtc; }
bool Rtc::now(DateTime& dt) { dt = fake::stored; return fake::rtcReadable; }
bool Rtc::set(const DateTime& dt) {
  ++fake::rtcWrites;
  if (!fake::rtcWritable) return false;
  fake::stored = dt;
  return true;
}
WiFiClass WiFi;
int WiFiClass::status() const { return fake::wifi ? WL_CONNECTED : 0; }
void testLog(const char*, const char* format, ...) {
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  fake::logs += std::string(text) + '\n';
}
struct HttpDownloader {
  using ProgressCallback = std::function<void(size_t, size_t)>;
  enum DownloadError { OK, HTTP_ERROR, FILE_ERROR, ABORTED };
};
using esp_err_t = int;
constexpr int ESP_OK = 0, WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM = 1;
int esp_wifi_set_ps(int) { return ESP_OK; }
namespace base64 { String encode(const char*) { return ""; } }
namespace network_trust { const char* forUrl(const std::string&) { return "test-CA"; } }
namespace freeink {
class SecureHttpClient {
 public:
  void setTimeout(int) {}
  void setCACert(const char* ca) { fake::caConfigured = ca && *ca; }
  bool begin(const std::string&) { ++fake::tlsBegins; return fake::caConfigured; }
  void setUserAgent(const char*) {}
  void addHeader(const char*, const std::string&) {}
  int GET(const std::function<bool(const uint8_t*,size_t)>& body, const std::function<bool()>& cancel) {
    if (cancel()) return -1;
    const uint8_t bytes[] = {'b', 'o', 'o', 'k'};
    return body(bytes, sizeof(bytes)) ? 200 : -1;
  }
  int getStatus() const { return 200; }
  bool hasContentLength() const { return true; }
  size_t getContentLength() const { return 4; }
  bool aborted() const { return false; }
  bool callbackAborted() const { return false; }
  bool responseComplete() const { return true; }
  std::string getHeader(const char*) const { return ""; }
  static bool resolveUrl(const std::string&,const std::string&,std::string&) { return false; }
};
}
constexpr int HTTP_TIMEOUT_MS = 60000, MAX_REDIRECTS = 5;
#define CROSSPOINT_VERSION "test"
#include <production-http-flow.inc>
int failures = 0, cases = 0;
#define CHECK(condition) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #condition); } } while (0)
HttpDownloader::DownloadError download() {
  std::string output;
  Sink sink;
  sink.write = [&output](const uint8_t* bytes,size_t count) { output.append(reinterpret_cast<const char*>(bytes),count); return true; };
  const auto result = runGetWolf("https://catalog.test/book.epub", "", "", sink, false, nullptr, true);
  if (result == HttpDownloader::OK) CHECK(output == "book");
  return result;
}
void noRtcColdBoot() {
  ++cases; fake::reset(); halClock.begin();
  CHECK(!halClock.isAvailable());
  CHECK(download() == HttpDownloader::OK);
  CHECK(fake::ntpCalls == 1);
  CHECK(fake::rtcWrites == 0);
  CHECK(fake::tlsBegins == 1 && fake::caConfigured);
  CHECK(fake::epoch == fake::ntpEpoch);
}
void failedRtcReadAndWrite() {
  ++cases; fake::reset(); fake::rtc = true; fake::rtcWritable = false; halClock.begin();
  CHECK(download() == HttpDownloader::OK);
  CHECK(fake::ntpCalls == 1 && fake::rtcWrites == 1);
  CHECK(fake::tlsBegins == 1 && fake::caConfigured);
  CHECK(fake::logs.find("RTC write failed") != std::string::npos);
}
void goodRtcWrite() {
  ++cases; fake::reset(); fake::rtc = true; halClock.begin();
  CHECK(download() == HttpDownloader::OK);
  CHECK(fake::rtcWrites == 1 && fake::stored.year == 2026);
  CHECK(fake::stored.month == 9 && fake::stored.day == 19);
  CHECK(fake::tlsBegins == 1 && fake::caConfigured);
}
void ntpTimeout() {
  ++cases; fake::reset(); fake::ntpResponds = false; halClock.begin();
  CHECK(download() == HttpDownloader::HTTP_ERROR);
  CHECK(fake::ntpCalls == 1 && fake::ms == 5001);
  CHECK(fake::tlsBegins == 0 && fake::rtcWrites == 0);
}
void wifiDisconnected() {
  ++cases; fake::reset(); fake::wifi = false; halClock.begin();
  CHECK(download() == HttpDownloader::HTTP_ERROR);
  CHECK(fake::ntpCalls == 0 && fake::tlsBegins == 0);
}
void invalidCompletedEpoch() {
  ++cases; fake::reset(); fake::rtc = true; fake::ntpEpoch = 1; halClock.begin();
  CHECK(download() == HttpDownloader::HTTP_ERROR);
  CHECK(fake::ntpCalls == 1 && fake::rtcWrites == 0 && fake::tlsBegins == 0);
}
void alreadyValidEpoch() {
  ++cases; fake::reset(); halClock.begin(); fake::epoch = fake::ntpEpoch;
  CHECK(download() == HttpDownloader::OK);
  CHECK(fake::ntpCalls == 0 && fake::tlsBegins == 1 && fake::caConfigured);
}
void expectDate(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
  uint16_t actualYear = 0;
  uint8_t actualMonth = 0, actualDay = 0, actualHour = 0, actualMinute = 0;
  CHECK(halClock.getDateTime(actualYear, actualMonth, actualDay, actualHour, actualMinute));
  CHECK(actualYear == year && actualMonth == month && actualDay == day);
  CHECK(actualHour == hour && actualMinute == minute);
}
void noRtcDisplayAfterNtp() {
  ++cases; fake::reset(); halClock.begin();
  CHECK(halClock.syncFromNTP());
  expectDate(2026, 9, 19, 12, 0);
  char text[9] = {};
  CHECK(halClock.formatTime(text, sizeof(text), 76));
  CHECK(std::string(text) == "19:00");
}
void offlineMidnightAndRetainedWake() {
  ++cases; fake::reset(); halClock.begin();
  CHECK(halClock.syncFromNTP());
  fake::wifi = false;
  fake::epoch += 12 * 3600;
  expectDate(2026, 9, 20, 0, 0);
  // Recreate the application HAL while retaining the platform epoch, as on a
  // powered deep-sleep wake. Full battery power loss is covered separately.
  halClock = HalClock{};
  halClock.begin();
  expectDate(2026, 9, 20, 0, 0);
  CHECK(fake::ntpCalls == 1);
}
void fullPowerLossStaysUnknown() {
  ++cases; fake::reset(); halClock.begin();
  CHECK(halClock.syncFromNTP());
  fake::epoch = 0;
  fake::wifi = false;
  halClock = HalClock{};
  halClock.begin();
  uint16_t y = 0;
  uint8_t mo = 0, d = 0, h = 0, mi = 0;
  CHECK(!halClock.getDateTime(y, mo, d, h, mi));
  CHECK(!halClock.syncFromNTP());
}
void failedRtcWriteUsesFreshSystemTime() {
  ++cases; fake::reset(); fake::rtc = fake::rtcReadable = true;
  fake::stored = {2026, 9, 18, 3, 4, 5, 5};
  halClock.begin();
  expectDate(2026, 9, 18, 3, 4);
  fake::rtcWritable = false;
  CHECK(halClock.syncFromNTP());
  fake::ms += 1001;
  expectDate(2026, 9, 19, 12, 0);
}
void rtcReadFailureKeepsTimeAdvancing() {
  ++cases; fake::reset(); fake::rtc = fake::rtcReadable = true;
  fake::stored = {2026, 9, 19, 23, 59, 0, 6};
  halClock.begin();
  expectDate(2026, 9, 19, 23, 59);
  fake::rtcReadable = false;
  fake::epoch += 120;
  fake::ms += 120000;
  expectDate(2026, 9, 20, 0, 1);
}
void invalidRtcCalendarIsNotNormalized() {
  ++cases; fake::reset(); fake::rtc = fake::rtcReadable = true;
  fake::stored = {2026, 2, 31, 12, 0, 0, 0};
  halClock.begin();
  CHECK(fake::epoch == 0);
  uint16_t y = 0;
  uint8_t mo = 0, d = 0, h = 0, mi = 0;
  CHECK(!halClock.getDateTime(y, mo, d, h, mi));
}
void systemClockUpperBound() {
  ++cases; fake::reset(); halClock.begin();
  fake::epoch = 4102444800LL;  // 2100-01-01 UTC, outside supported date arithmetic.
  uint16_t y = 0;
  uint8_t mo = 0, d = 0, h = 0, mi = 0;
  CHECK(!halClock.getDateTime(y, mo, d, h, mi));
}
void invalidRtcTimeAndLeapDates() {
  ++cases;
  for (const Rtc::DateTime invalid : {Rtc::DateTime{2026, 2, 29, 0, 0, 0, 0},
                                    Rtc::DateTime{2026, 1, 1, 24, 0, 0, 0},
                                    Rtc::DateTime{2026, 1, 1, 0, 60, 0, 0},
                                    Rtc::DateTime{2026, 1, 1, 0, 0, 60, 0}}) {
    fake::reset(); fake::rtc = fake::rtcReadable = true; fake::stored = invalid;
    halClock.begin();
    uint16_t y = 0;
    uint8_t mo = 0, d = 0, h = 0, mi = 0;
    CHECK(!halClock.getDateTime(y, mo, d, h, mi));
    CHECK(fake::epoch == 0);
  }
  fake::reset(); fake::rtc = fake::rtcReadable = true;
  fake::stored = {2028, 2, 29, 23, 59, 59, 2};
  halClock.begin();
  expectDate(2028, 2, 29, 23, 59);
}
void retainedSystemBeatsStaleRtc() {
  ++cases; fake::reset(); fake::rtc = fake::rtcReadable = true;
  fake::stored = {2026, 9, 18, 3, 4, 5, 5};
  fake::epoch = fake::ntpEpoch;
  halClock.begin();
  expectDate(2026, 9, 19, 12, 0);
}
void invalidNtpUpperBound() {
  ++cases; fake::reset(); halClock.begin(); fake::ntpEpoch = 4102444800LL;
  CHECK(!halClock.syncFromNTP());
}
void failedSyncRetainsWorkingClock() {
  ++cases; fake::reset(); halClock.begin();
  CHECK(halClock.syncFromNTP());
  fake::ntpResponds = false;
  CHECK(!halClock.syncFromNTP());
  expectDate(2026, 9, 19, 12, 0);
}
int main() {
  noRtcColdBoot(); failedRtcReadAndWrite(); goodRtcWrite(); ntpTimeout(); wifiDisconnected();
  invalidCompletedEpoch(); alreadyValidEpoch();
  noRtcDisplayAfterNtp(); offlineMidnightAndRetainedWake(); fullPowerLossStaysUnknown();
  failedRtcWriteUsesFreshSystemTime(); rtcReadFailureKeepsTimeAdvancing();
  invalidRtcCalendarIsNotNormalized(); systemClockUpperBound();
  invalidRtcTimeAndLeapDates(); retainedSystemBeatsStaleRtc();
  invalidNtpUpperBound(); failedSyncRetainsWorkingClock();
  std::printf("%d scenarios, %d failures\n", cases, failures);
  return failures ? 1 : 0;
}
