#include "HalTiltSensor.h"

#include <Logging.h>

#include <cstdlib>
#ifdef TENOR_PRESS_PROBE
#include <BoardConfig.h>
#include <Wire.h>
#endif

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
  _baselineValid = false;
  _shakeRun = false;
  _shakeSettling = false;
  _faceDown = false;
  _flipLastValid = false;
  _flipCalm = false;
  _tapTried = false;
  _tapSeen = false;
  _tapPoses = 0;
  _knockPending = false;
  _isAwake = true;
  return true;
}

bool HalTiltSensor::deepSleep() {
  if (!_available) {
    return false;
  }

  // Asleep the chip keeps the setup it had without double tap.
  if (_tapArmed) disarmTap();
  if (!_sdkImu.sleep()) {
    LOG_ERR("GYR", "IMU sleep failed");
    return false;
  }

  clearPendingEvents();
  clearPendingVerticalEvents();
  _shakeEvent = false;
  _faceDownEvent = false;
  _faceUpEvent = false;
  _doubleTapEvent = false;
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
  // Double tap turned Off: the chip goes back to the setup it had without it.
  if (!_doubleTapEnabled && _tapArmed) disarmTap();

  const bool horizontalEnabled = mode != CrossPointTiltPageTurn::TILT_OFF;
  const bool verticalEnabled = verticalMode != CrossPointTiltPageTurn::TILT_OFF;
  // A hard shake and face down or up are watched on every screen, so their settings alone
  // keep the sensor awake.
#ifdef TENOR_PRESS_PROBE
  const bool probeLogging = _probeLogUntilMs != 0 && static_cast<long>(millis() - _probeLogUntilMs) < 0;
  const bool sampling = _shakeEnabled || _flipEnabled || _doubleTapEnabled || probeLogging;
#else
  const bool sampling = _shakeEnabled || _flipEnabled || _doubleTapEnabled;
#endif

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
  if (_doubleTapEnabled && !_tapArmed && !_tapTried) {
    _tapTried = true;
    _tapArmed = _sdkImu.enableTap(TAP_CONFIG);
    LOG_INF("GYR", "Tap engine %s", _tapArmed ? "on" : "failed");
    // The sensors were stopped and restarted: settle as after a wake.
    _wakeMs = millis();
    _baselineValid = false;
    _tapSeen = false;
    _tapPoses = 0;
    _knockPending = false;
    return;
  }

  const unsigned long now = millis();
  // Stabilization: discard readings during gyro startup transient
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }

  if (_knockPending) {
    if ((now - _knockMs) < KNOCK_CONFIRM_MS) return;
  } else if ((now - _lastPollMs) < POLL_INTERVAL_MS) {
    return;
  }
  _lastPollMs = now;

  Imu::Sample sample;
  if (!_sdkImu.read(sample)) {
    return;
  }
  const float gx = sample.gx;
  const float gy = sample.gy;
#ifdef TENOR_PRESS_PROBE
  if (probeLogging) {
    logSerial.printf("IMU:%lu,%d,%d,%d,%d,%d,%d\n", now, static_cast<int>(sample.ax * 1000.0f),
                     static_cast<int>(sample.ay * 1000.0f), static_cast<int>(sample.az * 1000.0f),
                     static_cast<int>(sample.gx), static_cast<int>(sample.gy), static_cast<int>(sample.gz));
  }
#endif
  // Whole mg. The baseline follows slowly (1/8 per poll), so it holds gravity and
  // the grip but not a jolt, and it is where the device was before a flick.
  int32_t mg[3] = {static_cast<int32_t>(sample.ax * 1000.0f), static_cast<int32_t>(sample.ay * 1000.0f),
                   static_cast<int32_t>(sample.az * 1000.0f)};
  if (_tapArmed && _baselineValid) {
    if (_knockPending) {
      // Each axis keeps the reading nearer the baseline: a knock is gone by now, a snap is not.
      _knockPending = false;
      for (int i = 0; i < 3; ++i) {
        if (std::abs(_knockMg[i] - _baselineMg[i]) < std::abs(mg[i] - _baselineMg[i])) mg[i] = _knockMg[i];
      }
    } else {
      int32_t lengthSq = 0;
      for (int i = 0; i < 3; ++i) lengthSq += (mg[i] - _baselineMg[i]) * (mg[i] - _baselineMg[i]);
      if (lengthSq > SHAKE_RUN_MG * SHAKE_RUN_MG) {
        for (int i = 0; i < 3; ++i) _knockMg[i] = mg[i];
        _knockPending = true;
        _knockMs = now;
        return;
      }
    }
  }
  int32_t jolt[3] = {};
  if (!_baselineValid) {
    for (int i = 0; i < 3; ++i) _baselineMg[i] = mg[i];
    _baselineValid = true;
  } else {
    for (int i = 0; i < 3; ++i) {
      jolt[i] = mg[i] - _baselineMg[i];
      _baselineMg[i] += jolt[i] / 8;
    }
  }
  if (_shakeEnabled) pollShake(now, mg, jolt);
  if (_flipEnabled) pollFlip(now, mg, gx, gy);
  if (_tapArmed) pollDoubleTap(now);

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
    settleFlick(_pendingSide, tiltAxis, mg, now);
    if (_inTilt) {
      // Wait for device to return to neutral before allowing next trigger
      if (fabsf(tiltAxis) < NEUTRAL_RATE_DPS) {
        _inTilt = false;
      }
    } else {
      // Check for new tilt gesture (with cooldown)
      if ((now - _lastTiltMs) >= COOLDOWN_MS) {
        const float sideRateDps = _confirmSide ? _menuSideRateDps : _rateThresholdDps;
        if (tiltAxis > sideRateDps) {
          if (_confirmSide) {
            startFlick(_pendingSide, tiltAxis, HELD_FORWARD, now);
          } else {
            emitTilt(HELD_FORWARD, now);
          }
          _hadActivity = true;
          _inTilt = true;
          _lastTiltMs = now;
          LOG_INF("GYR", "Forward Trigger=(%.1f) dps", tiltAxis);
        } else if (tiltAxis < -sideRateDps) {
          if (_confirmSide) {
            startFlick(_pendingSide, tiltAxis, HELD_BACK, now);
          } else {
            emitTilt(HELD_BACK, now);
          }
          _hadActivity = true;
          _inTilt = true;
          _lastTiltMs = now;
          LOG_INF("GYR", "Backward Trigger=(%.1f) dps", tiltAxis);
        }
      }
    }
  }

  if (verticalArmed) {
    settleFlick(_pendingRow, verticalAxis, mg, now);
    if (_inVerticalTilt) {
      if (fabsf(verticalAxis) < NEUTRAL_RATE_DPS) {
        _inVerticalTilt = false;
      }
    } else if ((now - _lastVerticalTiltMs) >= COOLDOWN_MS) {
      if (verticalAxis > _verticalRateThresholdDps) {
        startFlick(_pendingRow, verticalAxis, HELD_DOWN, now);
        _hadActivity = true;
        _inVerticalTilt = true;
        _lastVerticalTiltMs = now;
        LOG_INF("GYR", "Down Trigger=(%.1f) dps", verticalAxis);
      } else if (verticalAxis < -_verticalRateThresholdDps) {
        startFlick(_pendingRow, verticalAxis, HELD_UP, now);
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
  if (_heldTilt == 0 || _shakeMoving || _shakeRun || _shakeSettling || (now - _heldTiltMs) < POLL_INTERVAL_MS) return;
  raiseTiltEvents(_heldTilt);
  _heldTilt = 0;
}

void HalTiltSensor::startFlick(PendingFlick& flick, const float axis, const uint8_t bit, const unsigned long now) {
  flick.active = true;
  flick.sign = axis > 0 ? 1 : -1;
  flick.bit = bit;
  flick.ms = now;
  for (int i = 0; i < 3; ++i) flick.poseMg[i] = _baselineMg[i];
}

void HalTiltSensor::settleFlick(PendingFlick& flick, const float axis, const int32_t (&mg)[3],
                                const unsigned long now) {
  if (!flick.active) return;
  const unsigned long age = now - flick.ms;
  bool back = age <= FLICK_RETURN_MS && axis * flick.sign <= -FLICK_RETURN_DPS;
  if (!back && fabsf(axis) < FLICK_CALM_DPS) {
    // Within 22 degrees: cos^2 >= 55/64. 64-bit, the squares of two dot products overflow 32.
    int64_t dot = 0, poseSq = 0, nowSq = 0;
    for (int i = 0; i < 3; ++i) {
      dot += static_cast<int64_t>(flick.poseMg[i]) * mg[i];
      poseSq += static_cast<int64_t>(flick.poseMg[i]) * flick.poseMg[i];
      nowSq += static_cast<int64_t>(mg[i]) * mg[i];
    }
    back = dot > 0 && 64 * dot * dot >= 55 * poseSq * nowSq;
  }
  if (back) {
    flick.active = false;
    emitTilt(flick.bit, now);
  } else if (age > FLICK_WAIT_MS) {
    flick.active = false;
    LOG_INF("GYR", "Flick dropped: device left turned");
  }
}

void HalTiltSensor::pollShake(const unsigned long now, const int32_t (&mg)[3], const int32_t (&jolt)[3]) {
  const int32_t lengthSq = jolt[0] * jolt[0] + jolt[1] * jolt[1] + jolt[2] * jolt[2];
  const int32_t peakSq = _shakePeakMg * _shakePeakMg;
  constexpr int32_t runSq = SHAKE_RUN_MG * SHAKE_RUN_MG;
  // Past half the peak the hand is jolting: a held flick waits for it to settle.
  _shakeMoving = 4 * lengthSq > peakSq;
  // Still shaking after a shake: the tilt lock runs on.
  if (lengthSq > peakSq && static_cast<long>(now - _tiltLockUntilMs) < 0) _tiltLockUntilMs = now + SHAKE_TILT_LOCK_MS;

  // A snap that has run its course counts once the device is calm and back near where it was.
  if (_shakeSettling && lengthSq <= runSq && (now - _shakeSettleFromMs) >= SHAKE_SETTLE_MS) {
    _shakeSettling = false;
    // Within 40 degrees: cos^2 >= 37/64.
    int64_t dot = 0, poseSq = 0, nowSq = 0;
    for (int i = 0; i < 3; ++i) {
      dot += static_cast<int64_t>(_shakeSettlePoseMg[i]) * mg[i];
      poseSq += static_cast<int64_t>(_shakeSettlePoseMg[i]) * _shakeSettlePoseMg[i];
      nowSq += static_cast<int64_t>(mg[i]) * mg[i];
    }
    if (dot > 0 && 64 * dot * dot >= 37 * poseSq * nowSq && (!_shaken || (now - _lastShakeMs) >= SHAKE_REST_MS)) {
      _shaken = true;
      _lastShakeMs = now;
      _shakeEvent = true;
      _hadActivity = true;
      _heldTilt = 0;
      _tiltLockUntilMs = now + SHAKE_TILT_LOCK_MS;
      LOG_INF("GYR", "Shake peak^2=%ld mg^2", static_cast<long>(_shakeRunPeakSq));
    }
  }

  if (lengthSq > runSq) {
    if (!_shakeRun) {
      _shakeRun = true;
      _shakeRunStartMs = now;
      _shakeRunPeakSq = 0;
      // The baseline before this poll moved it: where the device was.
      for (int i = 0; i < 3; ++i) _shakeRunPoseMg[i] = mg[i] - jolt[i];
    }
    _shakeRunLastMs = now;
    if (lengthSq > _shakeRunPeakSq) {
      _shakeRunPeakSq = lengthSq;
      _shakeRunPeakZ = jolt[2];
    }
    return;
  }
  if (!_shakeRun) return;
  // The run is over: a snap if it peaked high enough, in the screen's plane (|z| at most
  // 0.8 of it, cos^2 41/64), and ended soon enough.
  _shakeRun = false;
  const unsigned long runMs = _shakeRunLastMs - _shakeRunStartMs;
  if (_shakeRunPeakSq <= peakSq || runMs > SHAKE_RUN_MAX_MS) return;
  if (64 * _shakeRunPeakZ * _shakeRunPeakZ > 41 * _shakeRunPeakSq) return;
  if (_shakeSettling) return;
  _shakeSettling = true;
  _shakeSettleFromMs = now;
  for (int i = 0; i < 3; ++i) _shakeSettlePoseMg[i] = _shakeRunPoseMg[i];
}

void HalTiltSensor::configureShake(const uint8_t action, const uint8_t strength) {
  _shakePeakMg = SHAKE_PEAK_MG_BY_STRENGTH[strength < 3 ? strength : 1];
  const bool enabled = action != 0;
  if (_shakeEnabled && !enabled) {
    // Anything held for a shake that can no longer come goes through.
    raiseTiltEvents(_heldTilt);
    _heldTilt = 0;
    _shakeEvent = false;
    _shakeMoving = false;
    _shakeRun = false;
    _shakeSettling = false;
    _tiltLockUntilMs = 0;
  }
  _shakeEnabled = enabled;
}

bool HalTiltSensor::wasShaken() {
  const bool val = _shakeEvent;
  _shakeEvent = false;
  return val;
}

void HalTiltSensor::pollFlip(const unsigned long now, const int32_t (&mg)[3], const float gx, const float gy) {
  if (_faceDown) {
    if (mg[2] < -FLIP_UP_MG) {
      _faceDown = false;
      _faceUpEvent = true;
      _hadActivity = true;
      LOG_INF("GYR", "Face up");
    }
  } else {
    bool calm = _flipLastValid && fabsf(gx) <= FLIP_CALM_DPS && fabsf(gy) <= FLIP_CALM_DPS;
    for (int i = 0; i < 3; ++i) {
      const int32_t step = mg[i] - _flipLastMg[i];
      calm = calm && step <= FLIP_CALM_MG && step >= -FLIP_CALM_MG;
    }
    const int32_t lengthSq = mg[0] * mg[0] + mg[1] * mg[1] + mg[2] * mg[2];
    if (calm && mg[2] > 0 && 64 * mg[2] * mg[2] >= 60 * lengthSq) {
      // The run starts at the poll this one was compared with.
      if (!_flipCalm) {
        _flipCalm = true;
        _flipCalmFromMs = _flipLastMs;
      }
      if (now - _flipCalmFromMs >= FLIP_DOWN_MS) {
        _flipCalm = false;
        _faceDown = true;
        _faceDownEvent = true;
        _hadActivity = true;
        LOG_INF("GYR", "Face down");
      }
    } else {
      _flipCalm = false;
    }
  }
  for (int i = 0; i < 3; ++i) _flipLastMg[i] = mg[i];
  _flipLastMs = now;
  _flipLastValid = true;
}

void HalTiltSensor::configureFlip(const uint8_t faceDownAction, const uint8_t faceUpAction) {
  const bool enabled = faceDownAction != 0 || faceUpAction != 0;
  if (_flipEnabled && !enabled) {
    _faceDown = false;
    _faceDownEvent = false;
    _faceUpEvent = false;
    _flipLastValid = false;
    _flipCalm = false;
  }
  _flipEnabled = enabled;
}

bool HalTiltSensor::wasTurnedFaceDown() {
  const bool val = _faceDownEvent;
  _faceDownEvent = false;
  return val;
}

bool HalTiltSensor::wasTurnedFaceUp() {
  const bool val = _faceUpEvent;
  _faceUpEvent = false;
  return val;
}

void HalTiltSensor::pollDoubleTap(const unsigned long now) {
  if (_tapPoses == 0 || now - _tapPoseMs >= TAP_POSE_MS) {
    for (int i = 0; i < 3; ++i) {
      _tapPoseMg[0][i] = _tapPoseMg[1][i];
      _tapPoseMg[1][i] = _baselineMg[i];
    }
    if (_tapPoses < 2) ++_tapPoses;
    _tapPoseMs = now;
  }
  uint8_t taps = 0;
  if (!_sdkImu.readTap(taps)) return;
#ifdef TENOR_PRESS_PROBE
  if (taps != 0) logSerial.printf("IMU_TAP:%lu,%u,seen=%d\n", now, taps, _tapSeen);
#endif
  // One report per time the flag comes up, whether reading it clears it or not.
  if (taps == 2 && !_tapSeen) {
    // Within 12 degrees: cos^2 >= 61/64.
    int64_t dot = 0, poseSq = 0, nowSq = 0;
    for (int i = 0; i < 3; ++i) {
      dot += static_cast<int64_t>(_tapPoseMg[0][i]) * _baselineMg[i];
      poseSq += static_cast<int64_t>(_tapPoseMg[0][i]) * _tapPoseMg[0][i];
      nowSq += static_cast<int64_t>(_baselineMg[i]) * _baselineMg[i];
    }
    if (_tapPoses == 2 && dot > 0 && 64 * dot * dot >= 61 * poseSq * nowSq) {
      _doubleTapEvent = true;
      _hadActivity = true;
      LOG_INF("GYR", "Double tap");
    } else {
      LOG_INF("GYR", "Double tap dropped: device moved");
    }
  }
  _tapSeen = taps != 0;
}

void HalTiltSensor::disarmTap() {
  if (!_sdkImu.disableTap()) LOG_ERR("GYR", "Tap engine off failed");
  _tapArmed = false;
  _tapTried = false;
  _tapSeen = false;
  _doubleTapEvent = false;
  _tapPoses = 0;
  _knockPending = false;
  // The sensors were stopped and restarted: settle as after a wake.
  _wakeMs = millis();
  _baselineValid = false;
}

bool HalTiltSensor::wasDoubleTapped() {
  const bool val = _doubleTapEvent;
  _doubleTapEvent = false;
  return val;
}

#ifdef TENOR_PRESS_PROBE
void HalTiltSensor::probeFastLog(const unsigned long ms) {
  if (!_available) return;
  const bool wasAwake = _isAwake;
  if (!wasAwake && !wake()) return;
  // Logged with the tap engine off; the next poll turns it back on.
  if (_tapArmed) disarmTap();
  // QMI8658 at its 224 Hz setting: CTRL2 accel +-2 g, CTRL3 gyro +-512 dps, ODR code 5.
  // The address is whichever one answers WHO_AM_I, as the SDK's begin() finds it.
  const auto& sensors = BoardConfig::ACTIVE.sensors;
  uint8_t addr = 0;
  for (const uint8_t candidate : {sensors.imuAddr, static_cast<uint8_t>(sensors.imuAddr == 0x6A ? 0x6B : 0x6A)}) {
    Wire.beginTransmission(candidate);
    Wire.write(0x00);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(candidate, uint8_t{1}, uint8_t{1}) == 1 &&
        Wire.read() == 0x05) {
      addr = candidate;
      break;
    }
  }
  const auto writeReg = [addr](const uint8_t reg, const uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  };
  const bool fast = sensors.imuType == BoardConfig::ImuType::Qmi8658 && addr != 0;
  if (fast) {
    writeReg(0x03, 0x05);
    writeReg(0x04, 0x55);
  }
  logSerial.printf("IMUF_BEGIN:fast=%d,t=%lu\n", fast, millis());
  delay(WAKE_STABILIZE_MS);
  const unsigned long start = millis();
  unsigned long nextUs = micros();
  while (millis() - start < ms) {
    Imu::Sample s;
    if (_sdkImu.read(s)) {
      logSerial.printf("IMUF:%lu,%d,%d,%d,%d,%d,%d\n", micros(), static_cast<int>(s.ax * 1000.0f),
                       static_cast<int>(s.ay * 1000.0f), static_cast<int>(s.az * 1000.0f), static_cast<int>(s.gx),
                       static_cast<int>(s.gy), static_cast<int>(s.gz));
    }
    nextUs += fast ? 4460 : 35700;
    while (static_cast<long>(micros() - nextUs) < 0) delay(1);
  }
  if (fast) {
    writeReg(0x03, 0x08);
    writeReg(0x04, 0x58);
  }
  logSerial.printf("IMUF_END:t=%lu\n", millis());
  if (!wasAwake) _isAwake = !deepSleep();
  _baselineValid = false;
}
#endif

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
  // Rows: measured nods peaked at 143 to 314 deg/sec, two of ten at 186 and 187, so Light is 180.
  static constexpr float ROW_RATE_BY_STRENGTH[] = {180.0f, 270.0f, 360.0f};
  // Menus poll at ~85 ms when idle and confirm the return, so side flicks start lower
  // there: measured tab flicks peaked at 161 to 512 deg/sec, three of ten under 270.
  static constexpr float MENU_SIDE_RATE_BY_STRENGTH[] = {160.0f, 190.0f, 250.0f};
  _rateThresholdDps = RATE_BY_STRENGTH[horizontal < 3 ? horizontal : 1];
  _menuSideRateDps = MENU_SIDE_RATE_BY_STRENGTH[horizontal < 3 ? horizontal : 1];
  _verticalRateThresholdDps = ROW_RATE_BY_STRENGTH[vertical < 3 ? vertical : 1];
}

void HalTiltSensor::noteScreen(const uint32_t screen) {
  if (screen == _screen) return;
  _screen = screen;
  clearPendingEvents();
  clearPendingVerticalEvents();
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
  _pendingSide.active = false;
  _hadActivity = false;
  // Intentionally preserve _inTilt so a held tilt doesn't retrigger on next poll
}

void HalTiltSensor::clearPendingVerticalEvents() {
  _tiltUpEvent = false;
  _tiltDownEvent = false;
  _heldTilt &= static_cast<uint8_t>(~(HELD_UP | HELD_DOWN));
  _pendingRow.active = false;
  // Same reasoning as clearPendingEvents: _inVerticalTilt stays put.
}
