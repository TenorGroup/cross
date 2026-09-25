#include "HalTiltSensor.h"

#include <Logging.h>

HalTiltSensor halTiltSensor;  // Singleton instance

void HalTiltSensor::begin() {
  _available = _sdkImu.begin();
  if (_available) {
    _initMs = millis();
    _lastPollMs = millis();
    // begin() leaves the sensors sampling; stand them by until tilt page turn
    // actually wakes them, so a disabled IMU doesn't drain the battery.
    if (!_sdkImu.sleep()) {
      LOG_ERR("GYR", "IMU standby failed");
    }
    LOG_INF("GYR", "SDK IMU initialized");
    return;
  }
  LOG_ERR("GYR", "SDK IMU not found");
}

bool HalTiltSensor::wake() {
  if (!_available) {
    return false;
  }

  if (!_sdkImu.wake()) {
    LOG_ERR("GYR", "IMU wake failed");
    return false;
  }

  _lastPollMs = millis();
  _lastTiltMs = millis();
  _lastVerticalTiltMs = millis();
  _wakeMs = millis();
  _shakeBaselineValid = false;
  _shakeCandidate = false;
  _isAwake = true;
  return true;
}

bool HalTiltSensor::deepSleep() {
  if (!_available) {
    return false;
  }

  if (!_sdkImu.sleep()) {
    LOG_ERR("GYR", "IMU sleep failed");
    return false;
  }

  clearPendingEvents();
  clearPendingVerticalEvents();
  _shakeEvent = false;
  _inTilt = false;
  _inVerticalTilt = false;
  _isAwake = false;
  return true;
}

void HalTiltSensor::update(const uint8_t mode, const uint8_t orientation, const bool gestureTargetActive) {
  // The vertical channel carries its own mode and gate, armed by the menu
  // screen that owns the row selection; the reader and the plain-activity
  // routes disarm it, so only a menu can move a row.
  const uint8_t verticalMode = _verticalMode;
  const bool verticalTargetActive = _verticalTargetActive;

  if (shouldDiscardPendingEvents(mode, gestureTargetActive)) {
    clearPendingEvents();
  }
  if (shouldDiscardPendingEvents(verticalMode, verticalTargetActive)) {
    clearPendingVerticalEvents();
  }

  if (!_available) {
    return;
  }

  const bool horizontalEnabled = mode != CrossPointTiltPageTurn::TILT_OFF;
  const bool verticalEnabled = verticalMode != CrossPointTiltPageTurn::TILT_OFF;
  // A hard shake is watched on every screen, so its setting alone keeps the sensor awake.
  const bool sampling = _shakeEnabled;

  // State machine: wake up or sleep based on the enabled flags
  if (!horizontalEnabled && !verticalEnabled && !sampling) {
    if (_isAwake) _isAwake = !deepSleep();
    return;
  }

  // An inactive reader, modal, or child screen must not retain a gesture for
  // the next foreground target. Keep the held-gesture latch intact so it has
  // to return to neutral before another event can fire.
  const bool horizontalArmed = horizontalEnabled && gestureTargetActive;
  const bool verticalArmed = verticalEnabled && verticalTargetActive;
  if (!horizontalArmed && !verticalArmed && !sampling) {
    return;
  }

  if (!_isAwake) {
    _isAwake = wake();
    return;
  }

  const unsigned long now = millis();
  // Stabilization: discard readings during gyro startup transient
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }

  if ((now - _lastPollMs) < POLL_INTERVAL_MS) {
    return;
  }
  _lastPollMs = now;

  Imu::Sample sample;
  if (!_sdkImu.read(sample)) {
    return;
  }
  const float gx = sample.gx;
  const float gy = sample.gy;
  if (_shakeEnabled) pollShake(now, sample);

  // Map the gyro axes to the screen axes based on reader orientation. On the
  // X3 PCB: X axis = left/right in portrait, Y axis = left/right in landscape,
  // and the screen's up/down axis is the remaining one, rotated the same way.
  float tiltAxis;
  float verticalAxis;
  switch (orientation) {
    case CrossPointOrientation::PORTRAIT:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gx : gx;
      verticalAxis = verticalMode == CrossPointTiltPageTurn::TILT_INVERTED ? -gy : gy;
      break;
    case CrossPointOrientation::INVERTED:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gx : -gx;
      verticalAxis = verticalMode == CrossPointTiltPageTurn::TILT_INVERTED ? gy : -gy;
      break;
    case CrossPointOrientation::LANDSCAPE_CW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gy : -gy;
      verticalAxis = verticalMode == CrossPointTiltPageTurn::TILT_INVERTED ? -gx : gx;
      break;
    case CrossPointOrientation::LANDSCAPE_CCW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gy : gy;
      verticalAxis = verticalMode == CrossPointTiltPageTurn::TILT_INVERTED ? gx : -gx;
      break;
    default:
      tiltAxis = gx;
      verticalAxis = gy;
      break;
  }

  if (horizontalArmed) {
    if (_inTilt) {
      // Wait for device to return to neutral before allowing next trigger
      if (fabsf(tiltAxis) < NEUTRAL_RATE_DPS) {
        _inTilt = false;
      }
    } else {
      // Check for new tilt gesture (with cooldown)
      if ((now - _lastTiltMs) >= COOLDOWN_MS) {
        if (tiltAxis > _rateThresholdDps) {
          emitTilt(HELD_FORWARD, now);
          _hadActivity = true;
          _inTilt = true;
          _lastTiltMs = now;
          LOG_INF("GYR", "Forward Trigger=(%.1f) dps", tiltAxis);
        } else if (tiltAxis < -_rateThresholdDps) {
          emitTilt(HELD_BACK, now);
          _hadActivity = true;
          _inTilt = true;
          _lastTiltMs = now;
          LOG_INF("GYR", "Backward Trigger=(%.1f) dps", tiltAxis);
        }
      }
    }
  }

  if (verticalArmed) {
    if (_inVerticalTilt) {
      if (fabsf(verticalAxis) < NEUTRAL_RATE_DPS) {
        _inVerticalTilt = false;
      }
    } else if ((now - _lastVerticalTiltMs) >= COOLDOWN_MS) {
      if (verticalAxis > _verticalRateThresholdDps) {
        emitTilt(HELD_DOWN, now);
        _hadActivity = true;
        _inVerticalTilt = true;
        _lastVerticalTiltMs = now;
        LOG_INF("GYR", "Down Trigger=(%.1f) dps", verticalAxis);
      } else if (verticalAxis < -_verticalRateThresholdDps) {
        emitTilt(HELD_UP, now);
        _hadActivity = true;
        _inVerticalTilt = true;
        _lastVerticalTiltMs = now;
        LOG_INF("GYR", "Up Trigger=(%.1f) dps", verticalAxis);
      }
    }
  }

  releaseHeldTilt(now);
}

void HalTiltSensor::raiseTiltEvents(const uint8_t bits) {
  if (bits & HELD_FORWARD) _tiltForwardEvent = true;
  if (bits & HELD_BACK) _tiltBackEvent = true;
  if (bits & HELD_UP) _tiltUpEvent = true;
  if (bits & HELD_DOWN) _tiltDownEvent = true;
}

void HalTiltSensor::emitTilt(const uint8_t heldBit, const unsigned long now) {
  if (!_shakeEnabled) {
    raiseTiltEvents(heldBit);
    return;
  }
  // Inside a shake the flick is part of it: dropped, and its latch still has to
  // come back to neutral before the next one.
  if (static_cast<long>(now - _tiltLockUntilMs) < 0) return;
  _heldTilt |= heldBit;
  _heldTiltMs = now;
}

void HalTiltSensor::releaseHeldTilt(const unsigned long now) {
  if (_heldTilt == 0 || _shakeCandidate || _shakeMoving || (now - _heldTiltMs) < POLL_INTERVAL_MS) return;
  raiseTiltEvents(_heldTilt);
  _heldTilt = 0;
}

void HalTiltSensor::pollShake(const unsigned long now, const Imu::Sample& sample) {
  // Whole mg. The baseline follows slowly (1/8 per poll), so it holds gravity and
  // the grip but not a jolt that reverses within a few polls.
  const int32_t mg[3] = {static_cast<int32_t>(sample.ax * 1000.0f), static_cast<int32_t>(sample.ay * 1000.0f),
                         static_cast<int32_t>(sample.az * 1000.0f)};
  if (!_shakeBaselineValid) {
    for (int i = 0; i < 3; ++i) _shakeBaseline[i] = mg[i];
    _shakeBaselineValid = true;
    _shakeMoving = false;
    return;
  }
  int32_t jolt[3];
  for (int i = 0; i < 3; ++i) {
    jolt[i] = mg[i] - _shakeBaseline[i];
    _shakeBaseline[i] += jolt[i] / 8;
  }
  if (_shakeCandidate && (now - _shakeCandidateMs) > SHAKE_WINDOW_MS) _shakeCandidate = false;

  const int32_t lengthSq = jolt[0] * jolt[0] + jolt[1] * jolt[1] + jolt[2] * jolt[2];
  // Past half the peak the hand is jolting: a held flick waits for it to settle.
  _shakeMoving = 4 * lengthSq > _shakePeakMg * _shakePeakMg;
  if (lengthSq <= _shakePeakMg * _shakePeakMg) return;

  // Still shaking after a shake: the tilt lock runs on.
  if (static_cast<long>(now - _tiltLockUntilMs) < 0) _tiltLockUntilMs = now + SHAKE_TILT_LOCK_MS;

  const int32_t dot = jolt[0] * _shakeCandidateMg[0] + jolt[1] * _shakeCandidateMg[1] + jolt[2] * _shakeCandidateMg[2];
  if (_shakeCandidate && dot < 0) {
    _shakeCandidate = false;
    if (_shaken && (now - _lastShakeMs) < SHAKE_REST_MS) return;
    _shaken = true;
    _lastShakeMs = now;
    _shakeEvent = true;
    _hadActivity = true;
    _heldTilt = 0;
    _tiltLockUntilMs = now + SHAKE_TILT_LOCK_MS;
    LOG_INF("GYR", "Shake jolt=(%ld,%ld,%ld) mg", static_cast<long>(jolt[0]), static_cast<long>(jolt[1]),
            static_cast<long>(jolt[2]));
    return;
  }
  _shakeCandidate = true;
  _shakeCandidateMs = now;
  for (int i = 0; i < 3; ++i) _shakeCandidateMg[i] = jolt[i];
}

void HalTiltSensor::configureShake(const uint8_t action, const uint8_t strength) {
  _shakePeakMg = SHAKE_PEAK_MG_BY_STRENGTH[strength < 3 ? strength : 1];
  const bool enabled = action != 0;
  if (_shakeEnabled && !enabled) {
    // Anything held for a shake that can no longer come goes through.
    raiseTiltEvents(_heldTilt);
    _heldTilt = 0;
    _shakeEvent = false;
    _shakeCandidate = false;
    _shakeMoving = false;
    _tiltLockUntilMs = 0;
  }
  _shakeEnabled = enabled;
}

bool HalTiltSensor::wasShaken() {
  const bool val = _shakeEvent;
  _shakeEvent = false;
  return val;
}


bool HalTiltSensor::wasTiltedForward() {
  const bool val = _tiltForwardEvent;
  _tiltForwardEvent = false;
  return val;
}

bool HalTiltSensor::wasTiltedBack() {
  const bool val = _tiltBackEvent;
  _tiltBackEvent = false;
  return val;
}

void HalTiltSensor::setStrength(const uint8_t horizontal, const uint8_t vertical) {
  // Light, Medium, Strong in deg/sec. The neutral re-arm rate stays fixed, so a
  // Light setting still needs the wrist to settle before the next flick.
  static constexpr float RATE_BY_STRENGTH[] = {190.0f, 270.0f, 360.0f};
  _rateThresholdDps = RATE_BY_STRENGTH[horizontal < 3 ? horizontal : 1];
  _verticalRateThresholdDps = RATE_BY_STRENGTH[vertical < 3 ? vertical : 1];
}

void HalTiltSensor::configureVerticalGesture(const uint8_t mode, const bool gestureTargetActive) {
  _verticalMode = mode;
  _verticalTargetActive = gestureTargetActive;
}

bool HalTiltSensor::wasTiltedUp() {
  const bool val = _tiltUpEvent;
  _tiltUpEvent = false;
  return val;
}

bool HalTiltSensor::wasTiltedDown() {
  const bool val = _tiltDownEvent;
  _tiltDownEvent = false;
  return val;
}

bool HalTiltSensor::hadActivity() {
  const bool val = _hadActivity;
  _hadActivity = false;
  return val;
}

void HalTiltSensor::clearPendingEvents() {
  _tiltForwardEvent = false;
  _tiltBackEvent = false;
  _heldTilt &= static_cast<uint8_t>(~(HELD_FORWARD | HELD_BACK));
  _hadActivity = false;
  // Intentionally preserve _inTilt so a held tilt doesn't retrigger on next poll
}

void HalTiltSensor::clearPendingVerticalEvents() {
  _tiltUpEvent = false;
  _tiltDownEvent = false;
  _heldTilt &= static_cast<uint8_t>(~(HELD_UP | HELD_DOWN));
  // Same reasoning as clearPendingEvents: _inVerticalTilt stays put.
}
