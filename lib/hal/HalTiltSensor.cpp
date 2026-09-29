#include "HalTiltSensor.h"

#include <Logging.h>

#include "TiltPickUp.h"

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
  _wakeMs = millis();
  _baselineValid = false;
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
  _inTilt = false;
  _isAwake = false;
  return true;
}

void HalTiltSensor::update(const uint8_t mode, const uint8_t orientation, const bool inReader) {
  if (!_available) {
    return;
  }

  // State machine: wake up or sleep based on the enabled flag
  if ((mode != CrossPointTiltPageTurn::TILT_OFF) && !_isAwake) {
    _isAwake = wake();
    return;
  } else if ((mode == CrossPointTiltPageTurn::TILT_OFF) && _isAwake) {
    _isAwake = !deepSleep();
    return;
  }

  // If disabled, skip the rest of the polling logic and avoid unnecessary I2C traffic in non-reader activities
  if ((mode == CrossPointTiltPageTurn::TILT_OFF) || !inReader) {
    // Off the reader the pose goes stale, and a flick still settling belongs to the page it started on.
    _baselineValid = false;
    _flickPending = false;
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
  const int32_t mg[3] = {static_cast<int32_t>(sample.ax * 1000.0f), static_cast<int32_t>(sample.ay * 1000.0f),
                         static_cast<int32_t>(sample.az * 1000.0f)};
  if (!_baselineValid) {
    // The pose a flick has to come back to is taken with the device still: back in the book from a
    // menu while it is still moving, no flick is armed until it settles.
    if (fabsf(gx) >= NEUTRAL_RATE_DPS || fabsf(gy) >= NEUTRAL_RATE_DPS) return;
    for (int i = 0; i < 3; ++i) _baselineMg[i] = mg[i];
    _baselineValid = true;
    return;
  }

  // Map the gyro axis to left/right tilt based on reader orientation.
  // On the X3 PCB: X axis = left/right in portrait, Y axis = left/right in landscape.
  float tiltAxis;
  switch (orientation) {
    case CrossPointOrientation::PORTRAIT:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gx : gx;
      break;
    case CrossPointOrientation::INVERTED:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gx : -gx;
      break;
    case CrossPointOrientation::LANDSCAPE_CW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gy : -gy;
      break;
    case CrossPointOrientation::LANDSCAPE_CCW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gy : gy;
      break;
    default:
      tiltAxis = gx;
      break;
  }

  if (_flickPending) {
    const auto verdict = TiltPickUp::settle(_flickPoseMg, mg, tiltAxis, now - _flickMs);
    if (verdict == TiltPickUp::Verdict::Flick) {
      _flickPending = false;
      (_flickForward ? _tiltForwardEvent : _tiltBackEvent) = true;
    } else if (verdict == TiltPickUp::Verdict::PickUp) {
      _flickPending = false;
      LOG_INF("GYR", "Tilt dropped: device picked up");
    }
  }

  if (_inTilt) {
    // Wait for device to return to neutral before allowing next trigger
    if (fabsf(tiltAxis) < NEUTRAL_RATE_DPS) {
      _inTilt = false;
    }
  } else {
    // Check for new tilt gesture (with cooldown)
    if ((now - _lastTiltMs) >= COOLDOWN_MS) {
      if (tiltAxis > RATE_THRESHOLD_DPS) {
        startFlick(true, now);
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        LOG_INF("GYR", "Forward Trigger=(%.1f) dps", tiltAxis);
      } else if (tiltAxis < -RATE_THRESHOLD_DPS) {
        startFlick(false, now);
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        LOG_INF("GYR", "Backward Trigger=(%.1f) dps", tiltAxis);
      }
    }
  }

  for (int i = 0; i < 3; ++i) _baselineMg[i] += (mg[i] - _baselineMg[i]) / 8;
}

// The page turns once TiltPickUp::settle() says the device is back at rest; picking it up drops it.
// A flick still waiting keeps its place: a swing back past the trigger rate before it comes to
// rest would otherwise turn the page the wrong way.
void HalTiltSensor::startFlick(const bool forward, const unsigned long now) {
  if (_flickPending) return;
  _flickPending = true;
  _flickForward = forward;
  _flickMs = now;
  for (int i = 0; i < 3; ++i) _flickPoseMg[i] = _baselineMg[i];
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

bool HalTiltSensor::hadActivity() {
  const bool val = _hadActivity;
  _hadActivity = false;
  return val;
}

void HalTiltSensor::clearPendingEvents() {
  _tiltForwardEvent = false;
  _tiltBackEvent = false;
  _flickPending = false;
  _hadActivity = false;
  // Intentionally preserve _inTilt so a held tilt doesn't retrigger on next poll
}
