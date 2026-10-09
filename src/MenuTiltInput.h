#pragma once

#include <CrossPointSettings.h>
#include <HalTiltSensor.h>

namespace menutilt {

struct Step {
  bool next = false;
  bool previous = false;
};

inline Step pollTabs(const uint8_t orientation, const bool active) {
  const uint8_t mode = active ? SETTINGS.tiltTabNavigation : CrossPointTiltPageTurn::TILT_OFF;
  halTiltSensor.update(mode, orientation, active);
  if (!active) return {};
  return {halTiltSensor.wasTiltedForward(), halTiltSensor.wasTiltedBack()};
}

inline Step pollRows(const bool active) {
  halTiltSensor.configureVerticalGesture(SETTINGS.tiltMenuNavigation, active);
  const bool previous = halTiltSensor.wasTiltedDown();
  const bool next = halTiltSensor.wasTiltedUp();
  return {next, previous};
}

}
