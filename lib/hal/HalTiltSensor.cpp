#include "HalTiltSensor.h"

#include <Logging.h>

HalTiltSensor halTiltSensor;  // Singleton instance

bool HalTiltSensor::readGyro(float& gx, float& gy, float& gz) const {
  Imu::Sample sample;
  if (!_sdkImu.read(sample)) return false;
  gx = sample.gx;
  gy = sample.gy;
  gz = sample.gz;
  return true;
}

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

  // State machine: wake up or sleep based on the enabled flags
  if (!horizontalEnabled && !verticalEnabled) {
    if (_isAwake) _isAwake = !deepSleep();
    return;
  }

  // An inactive reader, modal, or child screen must not retain a gesture for
  // the next foreground target. Keep the held-gesture latch intact so it has
  // to return to neutral before another event can fire.
  const bool horizontalArmed = horizontalEnabled && gestureTargetActive;
  const bool verticalArmed = verticalEnabled && verticalTargetActive;
  if (!horizontalArmed && !verticalArmed) {
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

  float gx, gy, gz;
  if (!readGyro(gx, gy, gz)) {
    return;
  }

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
        if (tiltAxis > RATE_THRESHOLD_DPS) {
          _tiltForwardEvent = true;
          _hadActivity = true;
          _inTilt = true;
          _lastTiltMs = now;
          LOG_INF("GYR", "Forward Trigger=(%.1f) dps", tiltAxis);
        } else if (tiltAxis < -RATE_THRESHOLD_DPS) {
          _tiltBackEvent = true;
          _hadActivity = true;
          _inTilt = true;
          _lastTiltMs = now;
          LOG_INF("GYR", "Backward Trigger=(%.1f) dps", tiltAxis);
        }
      }
    }
  }

  if (!verticalArmed) {
    return;
  }

  if (_inVerticalTilt) {
    if (fabsf(verticalAxis) < NEUTRAL_RATE_DPS) {
      _inVerticalTilt = false;
    }
    return;
  }
  if ((now - _lastVerticalTiltMs) < COOLDOWN_MS) {
    return;
  }
  if (verticalAxis > RATE_THRESHOLD_DPS) {
    _tiltDownEvent = true;
    _hadActivity = true;
    _inVerticalTilt = true;
    _lastVerticalTiltMs = now;
    LOG_INF("GYR", "Down Trigger=(%.1f) dps", verticalAxis);
  } else if (verticalAxis < -RATE_THRESHOLD_DPS) {
    _tiltUpEvent = true;
    _hadActivity = true;
    _inVerticalTilt = true;
    _lastVerticalTiltMs = now;
    LOG_INF("GYR", "Up Trigger=(%.1f) dps", verticalAxis);
  }
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
  _hadActivity = false;
  // Intentionally preserve _inTilt so a held tilt doesn't retrigger on next poll
}

void HalTiltSensor::clearPendingVerticalEvents() {
  _tiltUpEvent = false;
  _tiltDownEvent = false;
  // Same reasoning as clearPendingEvents: _inVerticalTilt stays put.
}
