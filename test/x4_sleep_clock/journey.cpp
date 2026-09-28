// Runs the device sleep helper from src/main.cpp (extracted verbatim by
// journey.py into helper.inc) through the complete production HalPowerManager
// and SDK PowerManager, and prints every recorded boundary call as JSON.
#include "HalPowerManager.h"
#include <BoardConfig.h>
#include <esp_sleep.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "fakes.inc"

HalGPIO gpio;
// Battery on its own power: the shown percentage never takes the charging path here.
bool HalGPIO::isUsbConnected() const { return lastUsbConnected; }
struct ClockBoundary {
  bool valid = false;
  bool hasValidTime() const { return valid; }
} halClock;
// The value a settings card stored in its "wakeButtons" key. Older firmware
// loaded it into this field; journey.py compares every stored value against
// the power-button-only trace.
struct SettingsBoundary {
  uint8_t wakeButtons = 0;
} SETTINGS;

#include "helper.inc"

static std::string quoted(const std::string& value) { return "\"" + value + "\""; }

int main() {
  std::printf("{");
  bool first = true;
  for (unsigned wake = 0; wake <= 3; ++wake) {
    for (const auto board : {BoardConfig::Board::X3, BoardConfig::Board::X4}) {
      for (const bool valid : {false, true}) {
        fake::reset(board);
        // The power button that started sleep is still held for two reads.
        fake::pressedReads = 2;
        halClock.valid = valid;
        SETTINGS.wakeButtons = static_cast<uint8_t>(wake);
        bool slept = false;
        try {
          DEVICE_SLEEP_HELPER();
        } catch (const fake::Sleep&) {
          slept = true;
        }
        std::string trace;
        for (const auto& event : fake::trace) trace += (trace.empty() ? "" : ",") + quoted(event);
        const std::string name = std::string(board == BoardConfig::Board::X3 ? "x3" : "x4") + "-clock" +
                                 (valid ? "1" : "0") + "-card" + std::to_string(wake);
        std::printf("%s\n%s:{\"slept\":%s,\"lightSleeps\":%d,\"ms\":%lu,\"gpio13\":%d,\"held13\":%s,\"trace\":[%s]}",
                    first ? "" : ",", quoted(name).c_str(), slept ? "true" : "false", fake::lightSleeps, fake::ms,
                    fake::levels[13], fake::held[13] ? "true" : "false", trace.c_str());
        first = false;
      }
    }
  }
  std::printf("\n}\n");
  return 0;
}
