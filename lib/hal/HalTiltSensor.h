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
  // Screen the pending flicks started on (noteScreen).
  uint32_t _screen = 0;
  bool _tiltUpEvent = false;             // Consumed by wasTiltedUp()
  bool _tiltDownEvent = false;           // Consumed by wasTiltedDown()
  bool _inVerticalTilt = false;          // Currently tilted past threshold
  unsigned long _lastVerticalTiltMs = 0;  // Debounce / cooldown

  // Trigger speed per axis, set by setStrength(). Medium is the original
  // shared 270 deg/sec, so an untouched setting behaves as before.
  float _rateThresholdDps = 270.0f;
  float _menuSideRateDps = 190.0f;  // Side flicks on menus, which wait for the return
  float _verticalRateThresholdDps = 270.0f;

  // Tuning constants
  static constexpr float NEUTRAL_RATE_DPS = 50.0f;         // Must stop moving below this rate before next trigger
  static constexpr unsigned long COOLDOWN_MS = 600;        // Minimum ms between triggers
  static constexpr unsigned long POLL_INTERVAL_MS = 50;    // 20 Hz polling
  static constexpr unsigned long WAKE_STABILIZE_MS = 300;  // Ignore readings after wake

  mutable unsigned long _lastPollMs = 0;

  // Slow baseline of the acceleration in whole mg (1/8 per poll): gravity and how
  // the device is held. What is left over is the jolt of a poll.
  bool _baselineValid = false;
  int32_t _baselineMg[3] = {};

  // Hard shake channel, watched on every screen while its action is not Off. A
  // shake is one snap: a run of polls whose jolt stays past SHAKE_RUN_MG, peaks
  // past the strength's peak mostly in the screen's plane (a nod jolts across it),
  // is over within SHAKE_RUN_MAX_MS, and SHAKE_SETTLE_MS later leaves the device
  // within 40 degrees of where it was (picking it up does not). Squared lengths
  // and dot products, no square root.
  bool _shakeEnabled = false;
  bool _shakeEvent = false;  // Consumed by wasShaken()
  int32_t _shakePeakMg = 1500;
  bool _shakeMoving = false;  // Last jolt past half the peak
  bool _shakeRun = false;     // Polls in a row past SHAKE_RUN_MG
  unsigned long _shakeRunStartMs = 0;
  unsigned long _shakeRunLastMs = 0;
  int32_t _shakeRunPeakSq = 0;
  int32_t _shakeRunPeakZ = 0;      // Screen-normal part of the peak jolt
  int32_t _shakeRunPoseMg[3] = {};  // Baseline when the run started
  bool _shakeSettling = false;      // A run passed, waiting to see where the device settles
  unsigned long _shakeSettleFromMs = 0;
  int32_t _shakeSettlePoseMg[3] = {};
  bool _shaken = false;  // A shake has fired since wake (the rest applies)
  unsigned long _lastShakeMs = 0;
  unsigned long _tiltLockUntilMs = 0;
  // Tilt events found while shake is on wait one poll here (bits below), and as
  // long as the hand still jolts past half the peak: a shake drops them, anything
  // else lets them through.
  uint8_t _heldTilt = 0;
  unsigned long _heldTiltMs = 0;
  static constexpr uint8_t HELD_FORWARD = 1, HELD_BACK = 2, HELD_UP = 4, HELD_DOWN = 8;

  // Shake peak per strength (Light, Medium, Strong), mg of jolt past the baseline; change only here.
  // X3 hand runs (shake-replay): put down <= 1030 mg; 1100 takes every snap, 1200 all but a 1178, 1700 all strong ones.
  static constexpr int32_t SHAKE_PEAK_MG_BY_STRENGTH[] = {1100, 1200, 1700};
  static constexpr int32_t SHAKE_RUN_MG = 600;
  // Measured: one snap stays past 600 mg for at most 327 ms, a pick-up for at least 573 ms.
  static constexpr unsigned long SHAKE_RUN_MAX_MS = 400;
  // Measured 100 ms after a run: snaps within 25 degrees of the start, pick-ups 65 or more.
  static constexpr unsigned long SHAKE_SETTLE_MS = 100;
  static constexpr unsigned long SHAKE_REST_MS = 1500;      // Minimum ms between two shakes
  static constexpr unsigned long SHAKE_TILT_LOCK_MS = 800;  // Tilts ignored after a shake's last jolt

  // Face down and face up, watched on every screen while either action is not Off. Face
  // down: gravity within 14 degrees of the screen's back (az > 0, az^2 >= 60/64 of the
  // length squared), each axis within FLIP_CALM_MG of the poll before, side and up/down
  // rotation under FLIP_CALM_DPS, for FLIP_DOWN_MS. Face up: after a face down, the
  // screen turned up past FLIP_UP_MG. On the X3 the screen faces up at az -1 g.
  bool _flipEnabled = false;
  bool _faceDown = false;       // A face down fired and the screen has not come back up
  bool _faceDownEvent = false;  // Consumed by wasTurnedFaceDown()
  bool _faceUpEvent = false;    // Consumed by wasTurnedFaceUp()
  bool _flipLastValid = false;
  int32_t _flipLastMg[3] = {};
  unsigned long _flipLastMs = 0;
  bool _flipCalm = false;  // Lying face down and calm since _flipCalmFromMs
  unsigned long _flipCalmFromMs = 0;
  // X3 hand runs (flip-replay): six placements lay still face down 552 ms or more,
  // wrist turns and pick-ups held a face-down pose at most 250 ms.
  static constexpr unsigned long FLIP_DOWN_MS = 350;
  static constexpr int32_t FLIP_CALM_MG = 250;
  static constexpr float FLIP_CALM_DPS = 30.0f;
  static constexpr int32_t FLIP_UP_MG = 400;

  // Double tap on the back, watched on every screen while its action is not Off, by the
  // chip's own tap engine (Imu::enableTap), whose flag each poll reads. Off never touches
  // the chip, so every other gesture reads the setup it always had. A double tap counts
  // only with the device where it was 0.5 to 1 s before (gravity within 12 degrees):
  // laying it down, turning it over or taking it off a table knocks twice as well, but
  // moves it. 26/09 knock run replay: taps held within 7.3 degrees, the others 17 or more.
  bool _doubleTapEnabled = false;
  bool _tapArmed = false;        // The chip runs the tap engine (accelerometer at 224 Hz)
  bool _tapTried = false;        // Arming failed since the last wake: not retried until the next
  bool _tapSeen = false;         // The last poll read the tap flag set
  bool _doubleTapEvent = false;  // Consumed by wasDoubleTapped()
  int32_t _tapPoseMg[2][3] = {};  // Baseline taken every TAP_POSE_MS, older first
  uint8_t _tapPoses = 0;          // Slots filled since the sensors last started
  unsigned long _tapPoseMs = 0;
  static constexpr unsigned long TAP_POSE_MS = 500;
  // With the tap engine the accelerometer runs at 224 Hz, where a knock is a spike of 9 to
  // 14 ms that the 28 Hz rate smoothed away, and an edge knock reads as a snap. A poll that
  // jolts past SHAKE_RUN_MG is read again KNOCK_CONFIRM_MS later (26/09 knock run replay).
  bool _knockPending = false;
  int32_t _knockMg[3] = {};
  unsigned long _knockMs = 0;
  static constexpr unsigned long KNOCK_CONFIRM_MS = 15;
  // Chosen on the 26/09 knock run at 224 Hz through a software model of the engine
  // (test/imu_double_tap), not yet measured on the chip: a peak past 0.65 g^2 of linear
  // acceleration that is quiet again (under 0.4 g^2) after 7 samples (31 ms), 18 samples
  // (80 ms) of quiet, and the second tap within 80 samples (357 ms) of the first; the
  // averages at 1/16 and 1/4, the datasheet's own example. Knocks measured 140 to 232 ms apart.
  static constexpr Imu::TapConfig TAP_CONFIG = {0, 7, 18, 80, 8, 32, 650, 400};

  // A flick on a menu counts once the hand has come back: the axis swings the
  // other way past FLICK_RETURN_DPS within FLICK_RETURN_MS, or stops (under
  // FLICK_CALM_DPS) with gravity back within 22 degrees of where it was. Picking
  // the device up turns it as fast but leaves it turned. Row flicks always wait
  // for this; side flicks only when confirmSideFlicks() says so (menus, not the
  // reader, whose page turns keep their speed). Measured cost: rows ~0.38 s, tabs ~0.2 s.
  struct PendingFlick {
    bool active = false;
    int8_t sign = 0;
    uint8_t bit = 0;
    unsigned long ms = 0;
    int32_t poseMg[3] = {};
  };
  PendingFlick _pendingSide;
  PendingFlick _pendingRow;
  bool _confirmSide = false;
  static constexpr float FLICK_RETURN_DPS = 150.0f;
  static constexpr unsigned long FLICK_RETURN_MS = 400;
  static constexpr float FLICK_CALM_DPS = 60.0f;
  static constexpr unsigned long FLICK_WAIT_MS = 600;  // No return by then: dropped (a slow nod took 580)

#ifdef TENOR_PRESS_PROBE
  unsigned long _probeLogUntilMs = 0;
#endif

  void pollShake(unsigned long now, const int32_t (&mg)[3], const int32_t (&jolt)[3]);
  void pollFlip(unsigned long now, const int32_t (&mg)[3], float gx, float gy);
  void pollDoubleTap(unsigned long now);
  void disarmTap();
  void startFlick(PendingFlick& flick, float axis, uint8_t bit, unsigned long now);
  void settleFlick(PendingFlick& flick, float axis, const int32_t (&mg)[3], unsigned long now);
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

  // Side flicks wait for the hand to come back before they count (menus), or
  // count at once (the reader). Called once per loop pass.
  void confirmSideFlicks(bool confirm) { _confirmSide = confirm; }

  // The screen in front, as the activity manager counts them. A flick still waiting for the hand
  // to come back belongs to the screen it started on, so another screen drops it. Called once per
  // loop pass, before the screen polls.
  void noteScreen(uint32_t screen);

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

  // Arms face down and face up from their settings: either action but 0 (Off) keeps
  // the sensor awake on every screen. Called once per loop pass.
  void configureFlip(uint8_t faceDownAction, uint8_t faceUpAction);

  // Returns true once per face down, and once per face up that follows one; consumed on read.
  bool wasTurnedFaceDown();
  bool wasTurnedFaceUp();

  // Arms double tap from its setting: any action but 0 (Off) keeps the sensor awake on
  // every screen with the chip's tap engine on. Called once per loop pass.
  void configureDoubleTap(uint8_t action) { _doubleTapEnabled = action != 0; }

  // Returns true once per double tap, consumed on read.
  bool wasDoubleTapped();

#ifdef TENOR_PRESS_PROBE
  // Measurement build, CMD:IMU_LOG: each poll prints its raw sample until
  // `untilMs`, and the sensor stays awake for it.
  void probeLogUntil(unsigned long untilMs) { _probeLogUntilMs = untilMs; }
  // CMD:IMU_LOG <s> fast: blocks for `ms`, sampling at the chip's 224 Hz, then
  // restores the 28 Hz rate the firmware reads at.
  void probeFastLog(unsigned long ms);
#endif

  // Returns true once per tilt-forward gesture (next page direction).
  // Consumed on read — subsequent calls return false until next gesture.
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
