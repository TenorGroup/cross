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
int16_t current = 10;
bool readable = true;
bool readBQ27220CurrentMA(int16_t* currentMa) {
  ++fakebattery::gaugeReads;
  *currentMa = current;
  return readable;
}
// readI2CReg16LE (the About screen's extra registers) has an inline fake in stubs/HalGPIO.h.
}  // namespace X3GPIO

#include "usb.inc"

// HalPowerManager.cpp's getDisplayedBatteryPercentage() links against the production global
// (unexercised here: nothing in this file calls it, only pollGauge()/getBatteryPercentage()).
HalGPIO gpio;

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


void x3ChargingPollIsBounded() {
  boot(BoardConfig::Board::X3);
  HalGPIO local;
  X3GPIO::current = 10;
  X3GPIO::readable = true;
  const int before = fakebattery::gaugeReads;
  check(local.isUsbConnected(), "first charging sample is immediate");
  for (int i = 1; i < 250; ++i) {
    ++fake::ms;
    check(local.isUsbConnected(), "cached charging stays true before deadline");
  }
  check(fakebattery::gaugeReads == before + 1, "249 loop checks reuse one I2C sample");
  X3GPIO::current = -10;
  ++fake::ms;
  check(!local.isUsbConnected(), "unplug is seen at 250 ms");
  check(fakebattery::gaugeReads == before + 2, "deadline polls exactly once");
  fake::ms += 250;
  X3GPIO::readable = false;
  check(!local.isUsbConnected(), "failed gauge retains disconnected fallback");
  const int failedReads = fakebattery::gaugeReads;
  for (int i = 0; i < 100; ++i) local.isUsbConnected();
  check(fakebattery::gaugeReads == failedReads, "failed reads also wait for next deadline");
  faketask::current = &faketask::render;
  fake::ms += 1000;
  local.isUsbConnected();
  check(fakebattery::gaugeReads == failedReads, "render never polls an overdue gauge");
  faketask::current = &faketask::loop;
  X3GPIO::readable = true;
  X3GPIO::current = 10;
  fake::ms = UINT32_MAX - 100;
  check(local.isUsbConnected(), "poll before clock wrap");
  const int wrapReads = fakebattery::gaugeReads;
  fake::ms = 148;
  local.isUsbConnected();
  check(fakebattery::gaugeReads == wrapReads, "wrap keeps 249 ms cached");
  fake::ms = 149;
  local.isUsbConnected();
  check(fakebattery::gaugeReads == wrapReads + 1, "wrap polls at 250 ms");
}

int main() {
  x4LoopLeavesTheAdcAlone();
  x4DrawReadsTheDivider();
  x3LoopPollsTheGauge();
  x3FirstPaintChargingCheck();
  x3ChargingPollIsBounded();
  std::printf("%d assertions, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
