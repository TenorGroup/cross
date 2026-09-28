#include "HalPowerManager.h"
#include <BoardConfig.h>
#include <esp_sleep.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "fakes.inc"

// HalPowerManager.cpp's getDisplayedBatteryPercentage() links against the production global
// (unexercised here: this file only drives startDeepSleep()).
HalGPIO gpio;
// Battery on its own power: the shown percentage never takes the charging path here.
bool HalGPIO::isUsbConnected() const { return lastUsbConnected; }

int checks = 0, failures = 0, scenarios = 0;
void check(bool value, const char* label) {
  ++checks;
  if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
size_t position(const std::string& event) {
  return std::find(fake::trace.begin(), fake::trace.end(), event) - fake::trace.begin();
}
void sleep(bool preserve, bool useDefault = false) {
  ++scenarios;
  HalGPIO gpio;
  bool entered = false;
  try {
    if (useDefault) powerManager.startDeepSleep(gpio);
#if SLEEP_HAS_PRESERVE_ARG
    else powerManager.startDeepSleep(gpio, preserve);
#else
    else { (void)preserve; powerManager.startDeepSleep(gpio); }
#endif
  } catch (const fake::Sleep&) { entered = true; }
  check(entered, "production reached deep sleep");
  check(position("isolate") < position("global-hold") && position("global-hold") < position("sleep"),
        "isolate then global hold then sleep");
#if SOC_PM_SUPPORT_EXT1_WAKEUP
  check(position("ext1-wake:8:0") < position("isolate"), "S3 arms GPIO3 ext1 low before isolation");
#else
  check(position("gpio-wake:8:0") < position("isolate"), "C3 arms GPIO3 low before isolation");
#endif
  check(position("read:3:-1") < position("isolate"), "power release is sampled before sleeping");
}
void x4(bool preserve, bool useDefault = false) {
  fake::reset(BoardConfig::Board::X4); fake::pressedReads = 2;
  sleep(preserve, useDefault);
  const int desired = preserve && !useDefault ? 1 : 0;
  check(fake::levels[13] == desired && fake::held[13], "X4 latch holds expected level");
  const std::string write = "write:13:" + std::to_string(desired);
  check(position("release:13:-1") < position(write) && position(write) < position("hold:13:-1") &&
        position("hold:13:-1") < position("isolate"), "X4 release-write-hold precedes isolation");
  check(fake::lightSleeps == 0, "X4 goes straight to deep sleep");
  check(fake::ms >= 100, "power button held twice is released before sleeping");
}
void x3(bool preserve) {
  fake::reset(BoardConfig::Board::X3); sleep(preserve);
  check(fake::levels[13] == 0 && fake::held[13], "X3 SD rail stays off");
  check(position("write:13:1") == fake::trace.size(), "X3 never retains SD rail for clock");
  check(fake::lightSleeps == 0, "X3 goes straight to power-button deep sleep");
}
std::vector<std::string> other(bool preserve) {
  fake::reset(BoardConfig::Board::Other); sleep(preserve);
  check(position("write:13:1") == fake::trace.size() && position("write:13:0") == fake::trace.size(),
        "other board never drives GPIO13");
  check(fake::levels[1] == 1 && fake::held[1], "other board supply latch stays held high");
  check(fake::levels[5] == 1 && fake::held[5], "active-low SD rail is off");
  return fake::trace;
}
int main() {
#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  x4(true); x4(false); x4(true, true);
  x3(true); x3(false);
#endif
  const auto before = other(false);
  check(before == other(true), "preserve flag does not change other-board sleep trace");
  std::printf("%d scenarios, %d assertions, %d failures\n", scenarios, checks, failures);
  return failures ? 1 : 0;
}
