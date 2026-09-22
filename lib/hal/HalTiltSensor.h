#pragma once

#include <Arduino.h>
#include <Imu.h>

// TODO: Move enums into new header and share with CrossPointSettings.h
namespace CrossPointOrientation {
enum Value : uint8_t { PORTRAIT = 0, LANDSCAPE_CW = 1, INVERTED = 2, LANDSCAPE_CCW = 3 };
}

namespace CrossPointTiltPageTurn {
enum Value : uint8_t { TILT_OFF = 0, TILT_NORMAL = 1, TILT_INVERTED = 2 };
}

class HalTiltSensor;
extern HalTiltSensor halTiltSensor;  // Singleton

class HalTiltSensor {
  bool _available = false;
  mutable Imu _sdkImu;

  // Tilt gesture state machine
  bool _tiltForwardEvent = false;  // Consumed by wasTiltedForward()
  bool _tiltBackEvent = false;     // Consumed by wasTiltedBack()
  bool _hadActivity = false;       // Non-consuming flag for sleep timer
  bool _inTilt = false;            // Currently tilted past threshold
  bool _isAwake = false;           // Tracks power state
  unsigned long _initMs = 0;       // Timestamp of sensor init
  unsigned long _lastTiltMs = 0;   // Debounce / cooldown
  unsigned long _wakeMs = 0;       // Timestamp of last wake() for stabilization

  // Vertical channel: same state machine on the other gyro axis, with its own
  // mode, target gate, neutral latch and cooldown. Armed by
  // configureVerticalGesture(), read by wasTiltedUp()/wasTiltedDown().
  uint8_t _verticalMode = CrossPointTiltPageTurn::TILT_OFF;
  bool _verticalTargetActive = false;
  bool _tiltUpEvent = false;             // Consumed by wasTiltedUp()
  bool _tiltDownEvent = false;           // Consumed by wasTiltedDown()
  bool _inVerticalTilt = false;          // Currently tilted past threshold
  unsigned long _lastVerticalTiltMs = 0;  // Debounce / cooldown

  // Tuning constants
  static constexpr float RATE_THRESHOLD_DPS = 270.0f;      // Deg/sec speed to trigger flick
  static constexpr float NEUTRAL_RATE_DPS = 50.0f;         // Must stop moving below this rate before next trigger
  static constexpr unsigned long COOLDOWN_MS = 600;        // Minimum ms between triggers
  static constexpr unsigned long POLL_INTERVAL_MS = 50;    // 20 Hz polling
  static constexpr unsigned long WAKE_STABILIZE_MS = 300;  // Ignore readings after wake

  mutable unsigned long _lastPollMs = 0;

  bool readGyro(float& gx, float& gy, float& gz) const;

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  // Enables tilt polling state
  bool wake();

  // Puts tilt polling state to sleep
  bool deepSleep();

  // True if an IMU is present on this device
  bool isAvailable() const { return _available; }

  // Poll the accelerometer and update tilt gesture state for an active target.
  void update(const uint8_t mode, const uint8_t orientation, const bool gestureTargetActive);

  static bool shouldDiscardPendingEvents(const uint8_t mode, const bool gestureTargetActive) {
    return mode == CrossPointTiltPageTurn::TILT_OFF || !gestureTargetActive;
  }

  // Returns true once per tilt-forward gesture (next page direction).
  // Consumed on read - subsequent calls return false until next gesture.
  bool wasTiltedForward();

  // Returns true once per tilt-back gesture (previous page direction).
  // Consumed on read.
  bool wasTiltedBack();

  // Arms the vertical channel for the polls that follow. Menu screens call it
  // once per pass with their own setting and their own target gate; the
  // horizontal arguments of update() stay the reader/tab contract.
  // TILT_OFF, or an inactive target, means the channel never emits.
  void configureVerticalGesture(uint8_t mode, bool gestureTargetActive);

  // Returns true once per vertical gesture, consumed on read.
  // Sign convention: leaning the top edge of the device toward the user is Up
  // (one row back), pushing it away is Down (one row forward); TILT_INVERTED
  // swaps the two. NOT YET MEASURED ON AN X3 - the axis follows the same
  // orientation rotation as the horizontal channel, but the direction has to be
  // confirmed by hand on the device before it can be called correct.
  bool wasTiltedUp();
  bool wasTiltedDown();

  // Non-consuming: true if any tilt activity occurred since last call.
  // Used to reset the auto-sleep inactivity timer.
  bool hadActivity();

  // Discard any pending tilt events (call when leaving reader or disabling tilt).
  void clearPendingEvents();

  // Same, for the vertical channel alone.
  void clearPendingVerticalEvents();
};
