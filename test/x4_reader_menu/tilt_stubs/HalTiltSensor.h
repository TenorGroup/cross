#pragma once
#include <cstdint>
#include <string>
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
  void update(uint8_t mode, uint8_t orientation, bool active) {
    calls.push_back({mode, orientation, active});
    order += 'u';
  }
  bool wasTiltedForward() { const bool v = forward; forward = false; return v; }
  // The row channel, as the real sensor has it: armed by configureVerticalGesture, and a nod made while it is off or
  // its target inactive is dropped. `order` says in which order the menu asked: 'u' update, 'v' configure.
  struct Vertical {
    uint8_t mode, active;
  };
  std::vector<Vertical> verticalCalls;
  bool up = false, down = false;
  uint8_t verticalMode = 0;
  bool verticalActive = false;
  std::string order;
  void configureVerticalGesture(uint8_t mode, bool active) {
    verticalMode = mode;
    verticalActive = active;
    verticalCalls.push_back({mode, active});
    order += 'v';
  }
  bool wasTiltedUp() { const bool v = up && verticalMode != 0 && verticalActive; up = false; return v; }
  bool wasTiltedDown() { const bool v = down && verticalMode != 0 && verticalActive; down = false; return v; }
  bool wasTiltedBack() { const bool v = back; back = false; return v; }
};
extern HalTiltSensor halTiltSensor;
