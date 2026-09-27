#pragma once

#include <cstdint>

// Double taps, found in the accelerometer's samples from the chip's FIFO, which an X3 fills at
// 186 Hz (27/09): the tap detection of the QMI8658 datasheet (section 10.1) run in the firmware,
// since the chip's own engine never raised its flag on an X3 (26/09). Per sample the average
// follows the data by ALPHA, the linear acceleration is the data less that average, and its
// squared length starts a peak past PEAK_MG2; the movement average (by GAMMA) must be under
// QUIET_MG2 PEAK_WINDOW samples later for the peak to be a tap. After a first tap TAP_WINDOW
// samples must stay quiet; a second tap after them and within DOUBLE_TAP_WINDOW of the first is a
// double tap, none is a single tap. Other motion starts over. Whole mg and integers, no floating
// point.
//
// Where it landed: the linear acceleration over the first DIRECTION_SAMPLES of both taps. A tap
// pushes the device away from the finger, and the case rings back with the other sign only from
// the third or fourth sample (27/09 waveforms). Along z and negative it went into the back,
// along z and positive into the screen, along the screen's plane in from a side edge.
//
// The first tap counts only with the device still before it: no rotation past STILL_DPS on any
// axis over the STILL_SAMPLES before it. Switching the holding hand, putting the device down and
// turning it over knock like taps but turn the device as they do. Not the tap's own samples nor
// the second tap: a tap turns the device 100 to 200 dps as it lands, so the window before the
// second holds the first one's turn (27/09 replay: counting both dropped 33 of 39 back taps).
//
// Chosen through this code (test/imu_double_tap) on runs of one X3 held by hand:
// four read from the data registers (26-27/09, every place and the controls) and one from the
// FIFO as the device reads it. At 650 mg and 80 dps: 106 of 178 pairs at the right place on the
// register runs, one at a wrong one, none from the controls (page presses, two quick presses,
// switching hands, putting down, turning over, reading, a thumb on the screen); lower peaks or a
// looser stillness check found a few more and let controls through. On the FIFO run, 9 of 13 on
// the back, but a first tap on the screen lands near 460 mg and the edge's pairs were missed:
// the screen and the edge are experimental. Missed on every run: light taps on the screen, the
// screen in landscape, anything with the device lying on a table.
class TapDetector {
 public:
  enum class Place : uint8_t { Back, Screen, Edge };

  // Samples at 186 Hz, 5.4 ms each.
  static constexpr uint32_t PEAK_WINDOW = 7;         // 38 ms
  static constexpr uint32_t TAP_WINDOW = 18;         // 97 ms of quiet: a second tap 134 ms on at the earliest
  static constexpr uint32_t DOUBLE_TAP_WINDOW = 80;  // 430 ms
  static constexpr uint32_t DIRECTION_SAMPLES = 3;   // 16 ms
  static constexpr int32_t ALPHA = 8;                // In 1/128
  static constexpr int32_t GAMMA = 32;               // In 1/128
  static constexpr int32_t PEAK_MG2 = 422500;        // 650 mg
  static constexpr int32_t QUIET_MG2 = 400000;
  static constexpr uint32_t STILL_SAMPLES = 45;  // 242 ms
  static constexpr int32_t STILL_DPS = 80;

  // Starts over, as after samples that were lost.
  void reset() {
    _averageValid = false;
    _movement = 0;
    _state = IDLE;
    _n = 0;
    _turnAt = 0;
    _turnValid = false;
  }

  // Where the last double tap landed.
  Place lastPlace() const {
    const int32_t x = _direction[0] < 0 ? -_direction[0] : _direction[0];
    const int32_t y = _direction[1] < 0 ? -_direction[1] : _direction[1];
    const int32_t z = _direction[2] < 0 ? -_direction[2] : _direction[2];
    if (z < x || z < y) return Place::Edge;
    return _direction[2] < 0 ? Place::Back : Place::Screen;
  }

  // One sample: acceleration in mg, and the largest rotation on any axis in whole dps. Returns 0,
  // or 1 for a single tap and 2 for a double tap ending on it.
  int step(const int32_t ax, const int32_t ay, const int32_t az, const int32_t turnDps) {
    const int32_t a[3] = {ax, ay, az};
    if (!_averageValid) {
      for (int i = 0; i < 3; ++i) _average[i] = a[i] * 128;
      _averageValid = true;
    }
    // The average is kept in 1/128 mg, so the squared length comes out in mg^2 after >> 14.
    int64_t lengthSq = 0;
    int32_t linear[3];
    for (int i = 0; i < 3; ++i) {
      linear[i] = a[i] * 128 - _average[i];
      lengthSq += static_cast<int64_t>(linear[i]) * linear[i];
      _average[i] += linear[i] * ALPHA / 128;
    }
    const bool inPeak = _state == FIRST_PEAK || _state == SECOND_PEAK;
    if (inPeak && _n - _peakStart < DIRECTION_SAMPLES) {
      for (int i = 0; i < 3; ++i) _direction[i] += linear[i] / 128;
    }
    const int32_t mag = static_cast<int32_t>(lengthSq >> 14);
    _movement += static_cast<int32_t>(static_cast<int64_t>(mag - _movement) * GAMMA / 128);
    // Still up to this sample, not counting it: the tap itself turns the device 100 to 200 dps as
    // it lands (27/09), so counting its own sample would drop every real tap.
    const bool still = !_turnValid || _n - _turnAt >= STILL_SAMPLES;
    if (turnDps >= STILL_DPS) {
      _turnAt = _n;
      _turnValid = true;
    }
    int report = 0;
    switch (_state) {
      case IDLE:
        if (mag >= PEAK_MG2 && still) {
          _state = FIRST_PEAK;
          _peakStart = _firstStart = _n;
          for (int i = 0; i < 3; ++i) _direction[i] = linear[i] / 128;
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
          for (int i = 0; i < 3; ++i) _direction[i] += linear[i] / 128;
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
  int32_t _direction[3] = {};  // mg summed over the first DIRECTION_SAMPLES of both taps
  uint32_t _turnAt = 0;        // The last sample turning past STILL_DPS
  bool _turnValid = false;
};
