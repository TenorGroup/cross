#pragma once
#include <cstdint>
#include <vector>
namespace CrossPointTiltPageTurn {
enum Value : uint8_t { TILT_OFF = 0, TILT_NORMAL = 1, TILT_INVERTED = 2 };
}
// Records what the menu asks of the sensor and hands out the flicks a case sets.
struct HalTiltSensor {
  struct Call {
    uint8_t mode, orientation;
    bool active;
  };
  std::vector<Call> calls;
  bool forward = false, back = false;
  void update(uint8_t mode, uint8_t orientation, bool active) { calls.push_back({mode, orientation, active}); }
  bool wasTiltedForward() { const bool v = forward; forward = false; return v; }
  // The row channel: the menus of the lists use it, this one does not.
  void configureVerticalGesture(uint8_t, bool) {}
  bool wasTiltedUp() { return false; }
  bool wasTiltedDown() { return false; }
  bool wasTiltedBack() { const bool v = back; back = false; return v; }
};
extern HalTiltSensor halTiltSensor;
