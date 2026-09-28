#pragma once
#include <BoardConfig.h>

// Extra BQ27220 registers HalPowerManager.cpp's pollGaugeDiagnostics() reads (About screen
// rows); values match lib/hal/HalGPIO.h. gauge_task.cpp fakes the reader itself.
#define BQ27220_VOLT_REG 0x08
#define BQ27220_SOC_REG 0x2C
#define BQ27220_AVG_CUR_REG 0x14
#define BQ27220_RM_REG 0x10
#define BQ27220_FCC_REG 0x12
#define BQ27220_DC_REG 0x3C
#define BQ27220_SOH_REG 0x2E
#define BQ27220_CYCLE_COUNT_REG 0x2A
#define BQ27220_STATUS_REG 0x0A

namespace X3GPIO {
// gauge_task.cpp/behavior.cpp/journey.cpp compile HalPowerManager.cpp whole, so this needs a
// definition even where the test never exercises pollGaugeDiagnostics(); none of them assert
// on it, so one untracked, always-succeeding inline fake covers every harness here.
inline bool readI2CReg16LE(uint8_t, uint8_t, uint16_t* outValue) {
  *outValue = 0;
  return true;
}
}  // namespace X3GPIO

class HalGPIO {
 public:
  // isUsbConnected() is compiled from lib/hal/HalGPIO.cpp by gauge_task.py.
  bool lastUsbConnected = false;
  bool isUsbConnected() const;
  bool isXteinkDevice() const { return BoardConfig::ACTIVE.board != BoardConfig::Board::Other; }
  bool deviceIsX3() const { return BoardConfig::ACTIVE.board == BoardConfig::Board::X3; }
  bool deviceIsX4() const { return BoardConfig::ACTIVE.board == BoardConfig::Board::X4; }
  // Only the b2e808f sources call these two; they stay so journey.py
  // --source-root can still replay that tree against the same boundaries.
  uint8_t readWakeButtons() const { return 0; }
  static void markValidatedButtonWake(uint8_t) {}
};

// The production global (lib/hal/HalGPIO.h/.cpp): HalPowerManager.cpp's
// getDisplayedBatteryPercentage() reads gpio.isUsbConnected(), unexercised by these tests but
// still linked. Each .cpp that drives one of these harnesses defines it (journey.cpp already
// did, for its own reasons); this is only the declaration, same as the production header.
extern HalGPIO gpio;
