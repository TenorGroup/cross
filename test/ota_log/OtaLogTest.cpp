#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "src/network/OtaLog.h"

// The card log keeps one line per OTA check or install. These pin what a line says and how
// the file stays small, so a user's failure can be read back from the card.
namespace {
ota_log::Attempt stalledDownload() {
  ota_log::Attempt a;
  a.ok = false;
  a.step = "download";
  a.err = "HTTP_ERROR";
  a.http = 200;
  a.bytes = 1234567;
  a.total = 6351120;
  a.ms = 45678;
  a.xferMs = 40000;
  a.heap = 41234;
  a.largest = 19876;
  a.largestMin = 17000;
  a.idleMs = 10012;
  a.wd = "idle";
  a.rssi = -71;
  return a;
}

std::string lines(int count, size_t width) {
  std::string text;
  for (int i = 0; i < count; ++i) {
    std::string line = std::to_string(i);
    line.resize(width - 1, 'x');
    text += line + '\n';
  }
  return text;
}
}  // namespace

TEST(OtaLog, FieldsCarryEveryMeasurement) {
  char out[ota_log::LINE_BYTES];
  const size_t n = ota_log::formatFields(out, sizeof(out), stalledDownload());
  EXPECT_STREQ(out,
               "ok=0 step=download err=HTTP_ERROR http=200 bytes=1234567/6351120 ms=45678 xms=40000 kbs=30.1 "
               "heap=41234 largest=19876 lmin=17000 idle=10012 wd=idle rssi=-71");
  EXPECT_EQ(n, std::strlen(out));
}

TEST(OtaLog, RateIsZeroWithoutTransferTime) {
  ota_log::Attempt a;
  a.bytes = 500;
  char out[ota_log::LINE_BYTES];
  ota_log::formatFields(out, sizeof(out), a);
  EXPECT_NE(std::strstr(out, " kbs=0.0 "), nullptr) << out;
}

TEST(OtaLog, LineStartsWithUtcVersionAndOperation) {
  ota_log::Attempt a;
  a.ok = true;
  a.step = "done";
  a.err = "OK";
  a.http = 200;
  a.bytes = a.total = 6351120;
  a.ms = 70000;
  a.xferMs = 62000;
  char out[ota_log::LINE_BYTES];
  ota_log::formatLine(out, sizeof(out), 1790000000, "1.0.50", "install", a);
  EXPECT_STREQ(out,
               "2026-09-21T14:13:20Z v=1.0.50 op=install ok=1 step=done err=OK http=200 bytes=6351120/6351120 "
               "ms=70000 xms=62000 kbs=100.0 heap=0 largest=0 lmin=0 idle=0 wd=none rssi=0\n");
}

TEST(OtaLog, ClockNotSetPrintsDash) {
  char out[ota_log::LINE_BYTES];
  ota_log::formatLine(out, sizeof(out), 1000, "1.0.50", "check", ota_log::Attempt{});
  EXPECT_EQ(std::string(out).rfind("- v=1.0.50 op=check ok=0 step=none", 0), 0u) << out;
}

TEST(OtaLog, LongLineIsCutButStillEndsTheLine) {
  const std::string version(400, 'v');
  char out[ota_log::LINE_BYTES];
  const size_t n = ota_log::formatLine(out, sizeof(out), 0, version.c_str(), "install", stalledDownload());
  EXPECT_EQ(n, std::strlen(out));
  EXPECT_LT(n, sizeof(out));
  EXPECT_EQ(out[n - 1], '\n');
  EXPECT_EQ(std::strchr(out, '\n'), out + n - 1);
}

TEST(OtaLog, KeepsEveryLineUnderTheCap) {
  const std::string old = lines(ota_log::MAX_LINES - 1, 100);
  const auto kept = ota_log::keep(old.data(), old.size(), false, 100);
  EXPECT_EQ(kept.from, 0u);
  EXPECT_EQ(kept.to, old.size());
}

TEST(OtaLog, DropsOldestLinesPastTwenty) {
  const std::string old = lines(25, 100);
  const auto kept = ota_log::keep(old.data(), old.size(), false, 100);
  const std::string rest = old.substr(kept.from, kept.to - kept.from);
  EXPECT_EQ(rest, old.substr(6 * 100));  // 19 lines stay, the new one makes 20
  EXPECT_EQ(rest.rfind("6x", 0), 0u);
}

TEST(OtaLog, DropsOldestLinesPastTheByteCap) {
  const std::string old = lines(19, 250);
  const size_t added = 200;
  const auto kept = ota_log::keep(old.data(), old.size(), false, added);
  EXPECT_LE(kept.to - kept.from + added, ota_log::MAX_BYTES);
  EXPECT_GT(kept.to - kept.from + added + 250, ota_log::MAX_BYTES);  // no room was left unused
  EXPECT_EQ((kept.to - kept.from) % 250, 0u);                        // whole lines only
  EXPECT_EQ(kept.to, old.size());
}

TEST(OtaLog, TornLastLineIsDropped) {
  const std::string old = "a\nb\nhalf a li";
  const auto kept = ota_log::keep(old.data(), old.size(), false, 10);
  EXPECT_EQ(old.substr(kept.from, kept.to - kept.from), "a\nb\n");
}

TEST(OtaLog, TailReadStartsAtTheNextWholeLine) {
  const std::string old = "end of a cut line\nx\ny\n";
  const auto kept = ota_log::keep(old.data(), old.size(), true, 10);
  EXPECT_EQ(old.substr(kept.from, kept.to - kept.from), "x\ny\n");
  const std::string noBreak = "no line break anywhere";
  const auto none = ota_log::keep(noBreak.data(), noBreak.size(), true, 10);
  EXPECT_EQ(none.from, none.to);
}

TEST(OtaLog, EmptyFileKeepsNothing) {
  const auto kept = ota_log::keep("", 0, false, 10);
  EXPECT_EQ(kept.from, 0u);
  EXPECT_EQ(kept.to, 0u);
}

TEST(OtaLog, StepFollowsHowFarTheResponseGot) {
  EXPECT_STREQ(ota_log::transferStep(0, false), "connect");
  EXPECT_STREQ(ota_log::transferStep(200, false), "header");
  EXPECT_STREQ(ota_log::transferStep(404, true), "header");
  EXPECT_STREQ(ota_log::transferStep(302, true), "header");
  EXPECT_STREQ(ota_log::transferStep(200, true), "download");
  // A 192 KB part answers 206; a body cut short there is a broken download too.
  EXPECT_STREQ(ota_log::transferStep(206, true), "download");
}

TEST(OtaLog, WatchdogNamesTheTimeoutThatEndedIt) {
  EXPECT_STREQ(ota_log::watchdog(true, 20000, 10000), "back");
  EXPECT_STREQ(ota_log::watchdog(false, 10012, 10000), "idle");
  EXPECT_STREQ(ota_log::watchdog(false, 9950, 10000), "idle");
  EXPECT_STREQ(ota_log::watchdog(false, 9800, 10000), "none");
  EXPECT_STREQ(ota_log::watchdog(false, 3000, 10000), "none");
}
