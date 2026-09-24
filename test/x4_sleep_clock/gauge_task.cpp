// Who reads the battery, from which task. Compiled by gauge_task.py with the complete
// HalPowerManager.cpp, HalGPIO::isUsbConnected() taken verbatim from lib/hal/HalGPIO.cpp,
// and LOOP_POLL, the powerManager call the main loop makes on every pass (src/main.cpp).
//
// An X4 has no gauge: its battery is an ADC divider on the same ADC unit as the button
// ladder, which the button timer owns. The loop must not convert it on every pass (that
// takes the ADC from the timer and turns the smoothing into a 0.1 s average); the render
// task reads it when it draws the battery, as it did before the X3 gauge fix.
// An X3 has an I2C gauge, which only the loop task may talk to, from the first paint on.
#include "HalPowerManager.h"
#include <BoardConfig.h>
#include <esp_sleep.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "fakes.inc"

namespace X3GPIO {
bool readBQ27220CurrentMA(int16_t* currentMa) {
  ++fakebattery::gaugeReads;
  *currentMa = 0;
  return true;
}
}  // namespace X3GPIO

#include "usb.inc"

int checks = 0, failures = 0;
void check(bool value, const char* label) {
  ++checks;
  if (!value) {
    ++failures;
    std::printf("FAIL %s\n", label);
  }
}

// A boot as setup() runs it: a fresh power manager started on the loop task, some time
// after reset.
void boot(BoardConfig::Board board) {
  fake::reset(board);
  if (board == BoardConfig::Board::X3) BoardConfig::ACTIVE.batteryGauge.gaugeAddr = 0x55;
  fake::ms = 250;
  fakebattery::gaugeReads = fakebattery::millivoltReads = 0;
  faketask::current = &faketask::loop;
  powerManager = HalPowerManager();
  powerManager.begin();
}

void loopPasses(int passes) {
  faketask::current = &faketask::loop;
  for (int i = 0; i < passes; ++i) {
    powerManager.LOOP_POLL();
    fake::ms += 10;
  }
}

void x4LoopLeavesTheAdcAlone() {
  boot(BoardConfig::Board::X4);
  check(fakebattery::millivoltReads == 0, "X4 boot converts no battery millivolts");
  loopPasses(100);
  check(fakebattery::millivoltReads == 0, "X4 loop passes convert no battery millivolts");
  check(fakebattery::gaugeReads == 0, "X4 has no gauge to read");
}

void x4DrawReadsTheDivider() {
  boot(BoardConfig::Board::X4);
  const int before = fakebattery::millivoltReads;
  faketask::current = &faketask::render;
  for (int i = 0; i < 3; ++i) check(powerManager.getBatteryPercentage() == 80, "X4 draw shows the divider reading");
  check(fakebattery::millivoltReads == before + 3, "X4 reads the divider once per draw");
}

void x3LoopPollsTheGauge() {
  boot(BoardConfig::Board::X3);
  check(fakebattery::gaugeReads == 1, "X3 boot reads the gauge once for the first paint");
  loopPasses(100);
  check(fakebattery::gaugeReads == 1, "X3 loop reads the gauge at most every 1.5 s");
  fake::ms += 600;
  loopPasses(1);
  check(fakebattery::gaugeReads == 2, "X3 loop reads the gauge again after 1.5 s");
  faketask::current = &faketask::render;
  fake::ms += 5000;
  check(powerManager.getBatteryPercentage() == 80, "X3 draw shows the loop's reading");
  check(fakebattery::gaugeReads == 2, "X3 draw never reads the gauge");
  check(fakebattery::millivoltReads == 0, "X3 never converts an ADC battery reading");
}

// setup() routes to the first screen before the first loop pass, and the render task
// paints it at once: a charging check from that paint must not reach the gauge either.
void x3FirstPaintChargingCheck() {
  boot(BoardConfig::Board::X3);
  const int before = fakebattery::gaugeReads;
  HalGPIO gpio;
  faketask::current = &faketask::render;
  gpio.isUsbConnected();
  check(fakebattery::gaugeReads == before, "X3 render task charging check before the first loop pass reads no gauge");
  faketask::current = &faketask::loop;
  const int loopBefore = fakebattery::gaugeReads;
  gpio.isUsbConnected();
  check(fakebattery::gaugeReads == loopBefore + 1, "X3 loop task charging check reads the gauge");
}

int main() {
  x4LoopLeavesTheAdcAlone();
  x4DrawReadsTheDivider();
  x3LoopPollsTheGauge();
  x3FirstPaintChargingCheck();
  std::printf("%d assertions, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
