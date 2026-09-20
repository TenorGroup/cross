#include "HalPowerManager.h"
#include <BoardConfig.h>
#include <esp_sleep.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace fake {
std::vector<std::string> trace;
std::array<int, 64> levels;
std::array<bool, 64> held;
unsigned long ms = 0;
int pressedReads = 0, lightSleeps = 0;
struct Sleep {};
void event(const std::string& value) { trace.push_back(value); }
void pin(const char* action, int pin, int value = -1) {
  event(std::string(action) + ":" + std::to_string(pin) + ":" + std::to_string(value));
}
void reset(BoardConfig::Board board) {
  trace.clear(); levels.fill(-1); held.fill(false); ms = 0; pressedReads = lightSleeps = 0;
  BoardConfig::ACTIVE = {};
  BoardConfig::ACTIVE.board = board;
  if (board == BoardConfig::Board::X3) {
    BoardConfig::ACTIVE.power.latch0 = -1;
    BoardConfig::ACTIVE.sd.powerEnable = 13;
  } else if (board == BoardConfig::Board::Other) {
    BoardConfig::ACTIVE.power.latch0 = 1;
    BoardConfig::ACTIVE.display.rst = 9;
    BoardConfig::ACTIVE.sd.powerEnable = 5;
    BoardConfig::ACTIVE.sd.powerActiveHigh = false;
  }
  // Retained holds must be released before a new desired level can take effect.
  held[13] = true; levels[13] = 0;
}
}
SerialStub logSerial;
void SerialStub::end() { fake::event("serial-end"); }
void pinMode(int pin, int mode) { fake::pin("mode", pin, mode); }
void digitalWrite(int pin, int value) {
  fake::pin("write", pin, value);
  if (!fake::held[pin]) fake::levels[pin] = value;
}
int digitalRead(int pin) {
  fake::pin("read", pin);
  return fake::pressedReads-- > 0 ? LOW : HIGH;
}
unsigned long millis() { return fake::ms; }
void delay(unsigned long ms) { fake::ms += ms; fake::event("delay:" + std::to_string(ms)); }
int getCpuFrequencyMhz() { return 160; }
bool setCpuFrequencyMhz(int) { return true; }
void gpio_hold_dis(int pin) { fake::pin("release", pin); fake::held[pin] = false; }
void gpio_hold_en(int pin) { fake::pin("hold", pin); fake::held[pin] = true; }
void gpio_set_direction(int pin, int mode) { fake::pin("direction", pin, mode); }
void gpio_set_level(int pin, int level) { digitalWrite(pin, level); }
void gpio_deep_sleep_hold_en() { fake::event("global-hold"); }
int esp_sleep_disable_wakeup_source(int) { fake::event("disable-wake"); return ESP_OK; }
int esp_sleep_enable_timer_wakeup(uint64_t us) { fake::event("timer:" + std::to_string(us)); return ESP_OK; }
int esp_light_sleep_start() { ++fake::lightSleeps; return 1; }
int64_t esp_timer_get_time() { return static_cast<int64_t>(fake::ms) * 1000; }
int esp_deep_sleep_enable_gpio_wakeup(uint64_t mask, int level) {
  fake::event("gpio-wake:" + std::to_string(mask) + ":" + std::to_string(level)); return ESP_OK;
}
int esp_sleep_enable_ext1_wakeup(uint64_t mask, int level) {
  fake::event("ext1-wake:" + std::to_string(mask) + ":" + std::to_string(level)); return ESP_OK;
}
void esp_sleep_config_gpio_isolate() { fake::event("isolate"); }
[[noreturn]] void esp_deep_sleep_start() { fake::event("sleep"); throw fake::Sleep{}; }

int checks = 0, failures = 0, scenarios = 0;
void check(bool value, const char* label) {
  ++checks;
  if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
size_t position(const std::string& event) {
  return std::find(fake::trace.begin(), fake::trace.end(), event) - fake::trace.begin();
}
void sleep(bool preserve, uint8_t wakeMode = 0, bool useDefault = false) {
  ++scenarios;
  HalGPIO gpio;
  bool entered = false;
  try {
    if (useDefault) powerManager.startDeepSleep(gpio, wakeMode);
#if SLEEP_HAS_PRESERVE_ARG
    else powerManager.startDeepSleep(gpio, wakeMode, preserve);
#else
    else { (void)preserve; powerManager.startDeepSleep(gpio, wakeMode); }
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
void x4(bool preserve, uint8_t mode = 0, bool useDefault = false) {
  fake::reset(BoardConfig::Board::X4); fake::pressedReads = 2;
  sleep(preserve, mode, useDefault);
  const int desired = preserve && !useDefault ? 1 : 0;
  check(fake::levels[13] == desired && fake::held[13], "X4 latch holds expected level");
  const std::string write = "write:13:" + std::to_string(desired);
  check(position("release:13:-1") < position(write) && position(write) < position("hold:13:-1") &&
        position("hold:13:-1") < position("isolate"), "X4 release-write-hold precedes isolation");
  check(fake::lightSleeps == 0, "X4 never enters X3 ADC wake loop");
  check(fake::ms >= 100, "power button held twice is released before sleeping");
}
void x3(bool preserve, uint8_t mode = 0) {
  fake::reset(BoardConfig::Board::X3); sleep(preserve, mode);
  check(fake::levels[13] == 0 && fake::held[13], "X3 SD rail stays off");
  check(position("write:13:1") == fake::trace.size(), "X3 never retains SD rail for clock");
  check(fake::lightSleeps == (mode ? 3 : 0), "X3 wake mode retains existing rejected-sleep fallback");
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
  x4(true); x4(false); x4(true, 0, true); x4(true, 3);
  x3(true); x3(false); x3(true, 1);
#endif
  const auto before = other(false);
  check(before == other(true), "preserve flag does not change other-board sleep trace");
  std::printf("%d scenarios, %d assertions, %d failures\n", scenarios, checks, failures);
  return failures ? 1 : 0;
}
