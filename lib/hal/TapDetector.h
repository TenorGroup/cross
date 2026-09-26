#pragma once

#include <cstdint>

// Taps on the back, found in the accelerometer's samples at 224 Hz: the tap detection of the
// QMI8658 datasheet (section 10.1) run in the firmware, since the chip's own engine never
// raised its flag on an X3 (26/09). Per sample the average follows the data by ALPHA, the
// linear acceleration is the data less that average, and its squared length starts a peak
// past PEAK_MG2; the movement average (by GAMMA) must be under QUIET_MG2 PEAK_WINDOW samples
// later for the peak to be a tap. After a first tap TAP_WINDOW samples must stay quiet; a
// second tap after them and within DOUBLE_TAP_WINDOW of the first is a double tap, none is a
// single tap. Other motion starts over. Whole mg and integers, no floating point.
class TapDetector {
 public:
  // Chosen on the 26/09 knock run at 224 Hz (test/imu_double_tap): a peak past 0.65 g^2 that
  // is quiet again (under 0.4 g^2) after 7 samples (31 ms), 18 samples (80 ms) of quiet, and
  // the second tap within 80 samples (357 ms) of the first; the averages at 1/16 and 1/4, the
  // datasheet's own example. Knocks measured 140 to 232 ms apart.
  static constexpr uint32_t PEAK_WINDOW = 7;
  static constexpr uint32_t TAP_WINDOW = 18;
  static constexpr uint32_t DOUBLE_TAP_WINDOW = 80;
  static constexpr int32_t ALPHA = 8;   // In 1/128
  static constexpr int32_t GAMMA = 32;  // In 1/128
  static constexpr int32_t PEAK_MG2 = 650000;
  static constexpr int32_t QUIET_MG2 = 400000;

  // Starts over, as after samples that were lost.
  void reset() {
    _averageValid = false;
    _movement = 0;
    _state = IDLE;
    _n = 0;
  }

  // One sample in mg. Returns 0, or 1 for a single tap and 2 for a double tap ending on it.
  int step(const int32_t ax, const int32_t ay, const int32_t az) {
    const int32_t a[3] = {ax, ay, az};
    if (!_averageValid) {
      for (int i = 0; i < 3; ++i) _average[i] = a[i] * 128;
      _averageValid = true;
    }
    // The average is kept in 1/128 mg, so the squared length comes out in mg^2 after >> 14.
    int64_t lengthSq = 0;
    for (int i = 0; i < 3; ++i) {
      const int32_t linear = a[i] * 128 - _average[i];
      lengthSq += static_cast<int64_t>(linear) * linear;
      _average[i] += linear * ALPHA / 128;
    }
    const int32_t mag = static_cast<int32_t>(lengthSq >> 14);
    _movement += static_cast<int32_t>(static_cast<int64_t>(mag - _movement) * GAMMA / 128);
    int report = 0;
    switch (_state) {
      case IDLE:
        if (mag >= PEAK_MG2) {
          _state = FIRST_PEAK;
          _peakStart = _firstStart = _n;
        }
        break;
      case FIRST_PEAK:
        if (_n - _peakStart >= PEAK_WINDOW) _state = _movement < QUIET_MG2 ? QUIET : IDLE;
        break;
      case QUIET:
        if (_n - _firstStart >= TAP_WINDOW + PEAK_WINDOW) {
          _state = WAIT_SECOND;
        } else if (_movement >= QUIET_MG2 || mag >= PEAK_MG2) {
          _state = IDLE;
        }
        break;
      case WAIT_SECOND:
        if (_n - _firstStart >= DOUBLE_TAP_WINDOW) {
          report = 1;
          _state = IDLE;
        } else if (mag >= PEAK_MG2) {
          _state = SECOND_PEAK;
          _peakStart = _n;
        }
        break;
      case SECOND_PEAK:
        if (_n - _peakStart >= PEAK_WINDOW) {
          if (_movement < QUIET_MG2) report = 2;
          _state = IDLE;
        }
        break;
    }
    ++_n;
    return report;
  }

 private:
  enum State : uint8_t { IDLE, FIRST_PEAK, QUIET, WAIT_SECOND, SECOND_PEAK };
  bool _averageValid = false;
  State _state = IDLE;
  int32_t _average[3] = {};
  int32_t _movement = 0;
  uint32_t _n = 0;
  uint32_t _peakStart = 0;
  uint32_t _firstStart = 0;
};
