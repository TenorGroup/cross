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

  // Trigger speed per axis, set by setStrength(). Medium is the original
  // shared 270 deg/sec, so an untouched setting behaves as before.
  float _rateThresholdDps = 270.0f;
  float _verticalRateThresholdDps = 270.0f;

  // Tuning constants
  static constexpr float NEUTRAL_RATE_DPS = 50.0f;         // Must stop moving below this rate before next trigger
  static constexpr unsigned long COOLDOWN_MS = 600;        // Minimum ms between triggers
  static constexpr unsigned long POLL_INTERVAL_MS = 50;    // 20 Hz polling
  static constexpr unsigned long WAKE_STABILIZE_MS = 300;  // Ignore readings after wake

  mutable unsigned long _lastPollMs = 0;

  // Hard shake channel, watched on every screen while its action is not Off. A
  // shake is two strong jolts in opposite directions: the acceleration left after
  // a slow baseline (gravity and how the device is held) passes the peak, then
  // passes it again pointing the other way within SHAKE_WINDOW_MS. Squared
  // lengths and dot products in whole mg, no square root. A wrist flick that
  // turns a page rotates the device and stays under the peak.
  bool _shakeEnabled = false;
  bool _shakeEvent = false;  // Consumed by wasShaken()
  int32_t _shakePeakMg = 1500;
  bool _shakeBaselineValid = false;
  int32_t _shakeBaseline[3] = {};
  bool _shakeCandidate = false;  // One jolt seen, waiting for the opposite one
  bool _shakeMoving = false;     // Last jolt past half the peak
  int32_t _shakeCandidateMg[3] = {};
  unsigned long _shakeCandidateMs = 0;
  bool _shaken = false;  // A shake has fired since wake (the rest applies)
  unsigned long _lastShakeMs = 0;
  unsigned long _tiltLockUntilMs = 0;
  // Tilt events found while shake is on wait one poll here (bits below), and as
  // long as a jolt waits for its opposite or the hand still jolts past half the
  // peak: a shake drops them, anything else lets them through.
  uint8_t _heldTilt = 0;
  unsigned long _heldTiltMs = 0;
  static constexpr uint8_t HELD_FORWARD = 1, HELD_BACK = 2, HELD_UP = 4, HELD_DOWN = 8;

  // Shake peak per strength (Light, Medium, Strong), in mg of acceleration beyond
  // the baseline. Starting values under the +-2 g full scale, waiting on hand
  // measurements (IMU_LOG): the one place to change them.
  static constexpr int32_t SHAKE_PEAK_MG_BY_STRENGTH[] = {1200, 1500, 1800};
  static constexpr unsigned long SHAKE_WINDOW_MS = 400;     // Opposite jolt must follow within this
  static constexpr unsigned long SHAKE_REST_MS = 1500;      // Minimum ms between two shakes
  static constexpr unsigned long SHAKE_TILT_LOCK_MS = 800;  // Tilts ignored after a shake's last jolt

  void pollShake(unsigned long now, const Imu::Sample& sample);
  void raiseTiltEvents(uint8_t bits);
  void emitTilt(uint8_t heldBit, unsigned long now);
  void releaseHeldTilt(unsigned long now);

 public:
  // Call after BoardConfig has selected the active device.
  void begin();

  // Enables tilt polling state
  bool wake();

  // Puts tilt polling state to sleep
  bool deepSleep();

  // True if an IMU is present on this device
  bool isAvailable() const { return _available; }

  // True while the sensor is sampling (not in standby).
  bool isAwake() const { return _isAwake; }

  // Flick strength per axis (CrossPointSettings::TILT_STRENGTH): 0 Light,
  // 1 Medium, 2 Strong. Out-of-range values read as Medium.
  void setStrength(uint8_t horizontal, uint8_t vertical);

  // Poll the accelerometer and update tilt gesture state for an active target.
  void update(const uint8_t mode, const uint8_t orientation, const bool gestureTargetActive);

  static bool shouldDiscardPendingEvents(const uint8_t mode, const bool gestureTargetActive) {
    return mode == CrossPointTiltPageTurn::TILT_OFF || !gestureTargetActive;
  }

  // Arms the hard shake channel from its settings: any action but 0 (Off) keeps
  // the sensor awake on every screen and watches for shakes; strength is a
  // TILT_STRENGTH index (out of range reads as Medium). Called once per loop pass.
  void configureShake(uint8_t action, uint8_t strength);

  // Returns true once per hard shake, consumed on read.
  bool wasShaken();

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
