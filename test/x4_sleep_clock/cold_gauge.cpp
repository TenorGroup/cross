// The first seconds of a domestic X3 after a power-on reset, replayed through both layers that decide what the
// battery says: the raw reading HalPowerManager keeps (getBatteryPercentage, which the sleep-time Wi-Fi gate reads)
// and the number the screens draw (getDisplayedBatteryPercentage, battshown). Compiled by gauge_task.py --test with
// the complete HalPowerManager.cpp and BattShown.cpp.
//
// The gauge samples are the ones a real X3 logged after POWERON (261024-v1076/dev/log/x3nd-76c.log, line 66868):
// [148] soc=6, [3021] 6, [4534] 6, [6047] 6, [7555] 6, [9059] soc=81, [10568] 81. The battery was at 81%; the BQ27220
// answered what it held before the supply dropped for about nine seconds.
#include "HalPowerManager.h"
#include <BoardConfig.h>
#include <esp_sleep.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "fakes.inc"

#include "usb.inc"

HalGPIO gpio;

int checks = 0, failures = 0;
void check(bool value, const std::string& label) {
  ++checks;
  if (!value) {
    ++failures;
    std::printf("FAIL %s\n", label.c_str());
  }
}

uint16_t coldThenTrue() { return fake::ms < 9000 ? 6 : 81; }  // the x3nd-76c.log gauge, relative to boot
uint16_t sixForever() { return 6; }
uint16_t eightyOneAtOnce() { return 81; }

struct Seen {
  unsigned long at;
  int raw, shown;
};

// A boot as setup() runs it: the power manager reads the gauge at 148 ms, the state file (and the value the machine
// slept with) is loaded at 235 ms, then the loop polls and the render task draws every 100 ms, to `untilMs`.
std::vector<Seen> coldBoot(uint16_t (*gauge)(), int slept, unsigned long untilMs) {
  fake::reset(BoardConfig::Board::X3);
  BoardConfig::ACTIVE.batteryGauge.gaugeAddr = 0x55;
  fake::ms = 148;
  fakebattery::percentSource = gauge;
  faketask::current = &faketask::loop;
  powerManager = HalPowerManager();
  powerManager.begin();
  fake::ms = 235;
  if (slept >= 0) powerManager.holdSleepPercent(static_cast<uint8_t>(slept));
  std::vector<Seen> seen;
  for (; fake::ms <= untilMs; fake::ms += 10) {
    faketask::current = &faketask::loop;
    powerManager.LOOP_POLL();
    if (fake::ms % 100 == 5) {
      faketask::current = &faketask::render;
      const int shown = powerManager.getDisplayedBatteryPercentage();
      seen.push_back({fake::ms, powerManager.getBatteryPercentage(), shown});
    }
  }
  fakebattery::percentSource = nullptr;
  return seen;
}

int minShown(const std::vector<Seen>& v, unsigned long from, unsigned long to) {
  int m = 1000;
  for (const auto& s : v) if (s.at >= from && s.at < to) m = std::min(m, s.shown);
  return m;
}
int minRaw(const std::vector<Seen>& v, unsigned long from, unsigned long to) {
  int m = 1000;
  for (const auto& s : v) if (s.at >= from && s.at < to) m = std::min(m, s.raw);
  return m;
}
const Seen& at(const std::vector<Seen>& v, unsigned long ms) {
  for (const auto& s : v) if (s.at >= ms) return s;
  return v.back();
}

void theLoggedBootKeepsTheValueItSleptWith() {
  const auto v = coldBoot(coldThenTrue, 81, 20000);
  check(minRaw(v, 0, 20000) == 81, "raw layer never reports the cold 6 (slept 81)");
  check(minShown(v, 0, 20000) == 81, "display layer never draws the cold 6 (slept 81)");
  check(at(v, 15000).raw == 81 && at(v, 15000).shown == 81, "after the gauge settles both layers say 81");
}

void aSleepValueAboveTheTruthGivesWayOnceTheWindowEnds() {
  const auto v = coldBoot(coldThenTrue, 100, 20000);
  check(minShown(v, 0, 11900) == 100 && minRaw(v, 0, 11900) == 100, "held at the sleep value inside the window");
  check(at(v, 14000).raw == 81 && at(v, 14000).shown == 81, "the settled gauge wins once the window is over");
}

void aGaugeThatReallyReadsLowIsBelievedAfterTheWindow() {
  const auto v = coldBoot(sixForever, 81, 20000);
  check(at(v, 11000).raw == 81, "still the sleep value at 11 s");
  check(at(v, 14000).raw == 6 && at(v, 14000).shown == 6, "a battery that really is at 6% shows 6% from 13.5 s");
}

void aBatteryChargedWhileAsleepRisesAtOnce() {
  const auto v = coldBoot(eightyOneAtOnce, 6, 3000);
  check(at(v, 300).raw == 81 && at(v, 300).shown == 81, "a higher reading is never held back");
}

void withoutASleepValueNothingChanges() {
  const auto v = coldBoot(coldThenTrue, -1, 12000);
  check(at(v, 3000).raw == 6 && at(v, 3000).shown == 6, "no sleep value known: the gauge is believed as before");
  check(at(v, 11000).raw == 81, "and the real value still arrives");
}

void aBootThatNeverHeldTheValueDoesNotHoldIt() {
  // holdSleepPercent is only called after a power-on reset; any other boot reads the gauge as it is.
  fake::reset(BoardConfig::Board::X3);
  BoardConfig::ACTIVE.batteryGauge.gaugeAddr = 0x55;
  fake::ms = 148;
  fakebattery::percentSource = sixForever;
  powerManager = HalPowerManager();
  powerManager.begin();
  check(powerManager.getBatteryPercentage() == 6, "a warm gauge is read at face value");
  fakebattery::percentSource = nullptr;
}

int main() {
  theLoggedBootKeepsTheValueItSleptWith();
  aSleepValueAboveTheTruthGivesWayOnceTheWindowEnds();
  aGaugeThatReallyReadsLowIsBelievedAfterTheWindow();
  aBatteryChargedWhileAsleepRisesAtOnce();
  withoutASleepValueNothingChanges();
  aBootThatNeverHeldTheValueDoesNotHoldIt();
  std::printf("%d assertions, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
