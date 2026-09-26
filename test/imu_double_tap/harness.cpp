// The SDK's QMI8658 driver and the tilt sensor HAL on a fake chip: every I2C transaction
// is logged, and the chip answers the way its datasheet (QMI8658C Rev A, sections 5 and 10)
// describes. Its tap engine is a software model of the datasheet's text, not measured on a
// chip; the thresholds were chosen with it on the 26/09 knock run at 224 Hz.
#include <Wire.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "HalTiltSensor.h"

namespace {

unsigned long fakeMillis = 0;
int failures = 0;

[[maybe_unused]] bool expect(const bool condition, const std::string& scenario, const char* const detail) {
  if (condition) return true;
  std::printf("FAIL %s: %s\n", scenario.c_str(), detail);
  ++failures;
  return false;
}

// One logged sample: device microseconds, acceleration in mg, rotation in deg/sec.
struct Sample {
  std::string segment;
  unsigned long us;
  int ax, ay, az, gx, gy;
};

// ---- Software model of the tap engine, from the datasheet's section 10.1 -----------------
// Per accelerometer sample: the average follows the data by alpha, the linear acceleration is
// the data less that average, its squared length starts a peak past PeakMagThr, and the
// movement average (by gamma) must be under UDMThr at the end of PeakWindow for the peak to
// be a tap. After a first tap, TapWindow must stay quiet; a second tap after it and within
// DTapWindow of the first is a double tap, none is a single tap. Undefined motion restarts.
struct TapModel {
  uint8_t peakWindow = 0;
  uint16_t tapWindow = 0, doubleTapWindow = 0;
  float alpha = 0, gamma = 0, peak = 0, quiet = 0;
  float average[3] = {};
  bool averageValid = false;
  float movement = 0;
  enum State { Idle, FirstPeak, Quiet, WaitSecond, SecondPeak } state = Idle;
  unsigned long n = 0, peakStart = 0, firstStart = 0;

  void reset() {
    averageValid = false;
    movement = 0;
    state = Idle;
    n = 0;
  }

  // Returns 0, or 1 for a single tap and 2 for a double tap reported on this sample.
  int step(const float ax, const float ay, const float az) {
    const float a[3] = {ax, ay, az};
    if (!averageValid) {
      for (int i = 0; i < 3; ++i) average[i] = a[i];
      averageValid = true;
    }
    float mag = 0;
    for (int i = 0; i < 3; ++i) {
      const float linear = a[i] - average[i];
      mag += linear * linear;
      average[i] += alpha * (a[i] - average[i]);
    }
    movement += gamma * (mag - movement);
    int report = 0;
    switch (state) {
      case Idle:
        if (mag >= peak) {
          state = FirstPeak;
          peakStart = firstStart = n;
        }
        break;
      case FirstPeak:
        if (n - peakStart >= peakWindow) state = movement < quiet ? Quiet : Idle;
        break;
      case Quiet:
        if (n - firstStart >= static_cast<unsigned long>(tapWindow) + peakWindow) {
          state = WaitSecond;
        } else if (movement >= quiet || mag >= peak) {
          state = Idle;
        }
        break;
      case WaitSecond:
        if (n - firstStart >= doubleTapWindow) {
          report = 1;
          state = Idle;
        } else if (mag >= peak) {
          state = SecondPeak;
          peakStart = n;
        }
        break;
      case SecondPeak:
        if (n - peakStart >= peakWindow) {
          if (movement < quiet) report = 2;
          state = Idle;
        }
        break;
    }
    ++n;
    return report;
  }
};

// ---- The fake QMI8658 at 0x6B ---------------------------------------------------------
constexpr uint8_t CHIP_ADDR = 0x6B;

struct FakeChip {
  uint8_t regs[256] = {};
  uint8_t pointer = 0;
  bool completesCommands = true;
  bool tapClearsOnRead = true;
  bool logging = true;
  std::vector<std::string> log;
  uint8_t cal[2][6] = {};
  bool calSet[2] = {};
  TapModel model;
  bool engineWasOn = false;
  int engineReports[3] = {};  // by TAP_NUM

  // The motion fed to the chip: its samples at 224 Hz, from `startMs` on.
  const std::vector<Sample>* feed = nullptr;
  size_t next = 0;
  unsigned long startMs = 0;
  Sample latest{"", 0, 0, 0, -1000, 0, 0};
  // The 28 Hz output: every eighth sample, the mean of the last eight.
  Sample window[8] = {};
  int windowCount = 0;
  Sample held{"", 0, 0, 0, -1000, 0, 0};

  void reset() { *this = FakeChip{}; regs[0x00] = 0x05; }

  bool engineOn() const {
    return (regs[0x09] & 0x01) && (regs[0x08] & 0x01) && (regs[0x03] & 0x0F) == 0x05;
  }

  void noteEngine() {
    const bool on = engineOn();
    if (on && !engineWasOn) {
      model.reset();
      model.peakWindow = cal[0][0];
      model.tapWindow = static_cast<uint16_t>(cal[0][2] | cal[0][3] << 8);
      model.doubleTapWindow = static_cast<uint16_t>(cal[0][4] | cal[0][5] << 8);
      model.alpha = cal[1][0] / 128.0f;
      model.gamma = cal[1][1] / 128.0f;
      model.peak = static_cast<float>(cal[1][2] | cal[1][3] << 8) / 1000.0f;
      model.quiet = static_cast<float>(cal[1][4] | cal[1][5] << 8) / 1000.0f;
    }
    engineWasOn = on;
  }

  void write(const uint8_t reg, const uint8_t value) {
    regs[reg] = value;
    if (reg == 0x0A) {
      if (value == 0x0C && completesCommands) {
        const uint8_t set = regs[0x12];
        if (set == 1 || set == 2) {
          std::memcpy(cal[set - 1], &regs[0x0B], 6);
          calSet[set - 1] = true;
        }
        regs[0x2D] |= 0x80;
      } else if (value == 0x00) {
        regs[0x2D] &= static_cast<uint8_t>(~0x80);
      }
    }
    noteEngine();
  }

  static void put16(uint8_t* at, const int value) {
    const int clamped = std::max(-32768, std::min(32767, value));
    at[0] = static_cast<uint8_t>(clamped & 0xFF);
    at[1] = static_cast<uint8_t>((clamped >> 8) & 0xFF);
  }

  uint8_t read(const uint8_t reg) {
    if (reg >= 0x35 && reg <= 0x40) {
      const bool fastAccel = (regs[0x03] & 0x0F) == 0x05;
      const bool fastGyro = (regs[0x04] & 0x0F) == 0x05;
      const Sample& a = fastAccel ? latest : held;
      const Sample& g = fastGyro ? latest : held;
      uint8_t out[12];
      put16(&out[0], a.ax * 16384 / 1000);
      put16(&out[2], a.ay * 16384 / 1000);
      put16(&out[4], a.az * 16384 / 1000);
      put16(&out[6], g.gx * 64);
      put16(&out[8], g.gy * 64);
      put16(&out[10], 0);
      return out[reg - 0x35];
    }
    const uint8_t value = regs[reg];
    if (reg == 0x2F && tapClearsOnRead) regs[0x2F] &= static_cast<uint8_t>(~0x02);
    return value;
  }

  // Plays the fed samples up to `ms`.
  void advanceTo(const unsigned long ms) {
    if (!feed) return;
    while (next < feed->size()) {
      const Sample& s = (*feed)[next];
      const unsigned long at = startMs + ((s.us - (*feed)[0].us) / 1000);
      if (at > ms) break;
      latest = s;
      window[windowCount % 8] = s;
      if (++windowCount % 8 == 0) {
        Sample mean{"", s.us, 0, 0, 0, 0, 0};
        for (const auto& w : window) {
          mean.ax += w.ax;
          mean.ay += w.ay;
          mean.az += w.az;
          mean.gx += w.gx;
          mean.gy += w.gy;
        }
        mean.ax /= 8;
        mean.ay /= 8;
        mean.az /= 8;
        mean.gx /= 8;
        mean.gy /= 8;
        held = mean;
      }
      if (engineOn()) {
        const int report = model.step(s.ax / 1000.0f, s.ay / 1000.0f, s.az / 1000.0f);
        if (report != 0) {
          ++engineReports[report];
          regs[0x2F] |= 0x02;
          regs[0x59] = static_cast<uint8_t>(report | 0x30);
        }
      }
      ++next;
    }
  }

  // Holds one pose, as a device lying still.
  void hold(const int ax, const int ay, const int az) {
    latest = Sample{"", 0, ax, ay, az, 0, 0};
    held = latest;
  }
} chip;

std::vector<uint8_t> txBuffer;
uint8_t txAddr = 0;
std::vector<uint8_t> rxBuffer;
size_t rxNext = 0;

void logLine(const char* const format, const unsigned a, const unsigned b) {
  if (!chip.logging) return;
  char line[32];
  std::snprintf(line, sizeof(line), format, a, b);
  chip.log.push_back(line);
}

}  // namespace

unsigned long millis() { return fakeMillis; }
void delay(const unsigned long ms) { fakeMillis += ms; }

TwoWire Wire;
bool TwoWire::begin(int, int, uint32_t) { return true; }
void TwoWire::beginTransmission(const uint8_t addr) {
  txAddr = addr;
  txBuffer.clear();
}
size_t TwoWire::write(const uint8_t value) {
  txBuffer.push_back(value);
  return 1;
}
uint8_t TwoWire::endTransmission(bool) {
  if (txAddr != CHIP_ADDR || txBuffer.empty()) return 2;
  chip.pointer = txBuffer[0];
  if (txBuffer.size() == 2) {
    logLine("W %02X=%02X", txBuffer[0], txBuffer[1]);
    chip.write(txBuffer[0], txBuffer[1]);
  }
  return 0;
}
uint8_t TwoWire::requestFrom(const uint8_t addr, const uint8_t len, uint8_t) {
  if (addr != CHIP_ADDR) return 0;
  logLine("R %02X x%u", chip.pointer, len);
  rxBuffer.clear();
  rxNext = 0;
  for (uint8_t i = 0; i < len; ++i) rxBuffer.push_back(chip.read(static_cast<uint8_t>(chip.pointer + i)));
  return len;
}
int TwoWire::read() { return rxNext < rxBuffer.size() ? rxBuffer[rxNext++] : -1; }

namespace {

// One main loop pass as src/main.cpp runs it: every motion setting, then the owner's update.
struct Settings {
  uint8_t tilt = CrossPointTiltPageTurn::TILT_OFF;
  uint8_t rows = CrossPointTiltPageTurn::TILT_OFF;
  uint8_t shake = 0;
  uint8_t strength = 1;
  uint8_t faceDown = 0;
  uint8_t faceUp = 0;
  uint8_t doubleTap = 0;
  enum Screen { Plain, Reader, Menu } screen = Plain;
};

void pass(const Settings& s) {
  halTiltSensor.setStrength(1, 0);
  halTiltSensor.configureShake(s.shake, s.strength);
#ifdef HAVE_FLIP
  halTiltSensor.configureFlip(s.faceDown, s.faceUp);
#endif
#ifdef HAVE_DOUBLE_TAP
  halTiltSensor.configureDoubleTap(s.doubleTap);
#endif
  halTiltSensor.confirmSideFlicks(s.screen != Settings::Reader);
  if (s.screen == Settings::Reader) {
    halTiltSensor.configureVerticalGesture(CrossPointTiltPageTurn::TILT_OFF, false);
    halTiltSensor.update(s.tilt, CrossPointOrientation::PORTRAIT, true);
  } else if (s.screen == Settings::Menu) {
    halTiltSensor.update(s.tilt, CrossPointOrientation::PORTRAIT, true);
    halTiltSensor.configureVerticalGesture(s.rows, true);
  } else {
    halTiltSensor.configureVerticalGesture(CrossPointTiltPageTurn::TILT_OFF, false);
    halTiltSensor.update(CrossPointTiltPageTurn::TILT_OFF, CrossPointOrientation::PORTRAIT, false);
  }
}

void runFor(const Settings& s, const unsigned long ms) {
  const unsigned long end = fakeMillis + ms;
  while (fakeMillis < end) {
    fakeMillis += 10;
    chip.advanceTo(fakeMillis);
    pass(s);
  }
}

void boot() {
  fakeMillis = 0;
  chip.reset();
  chip.hold(-850, -100, -480);  // held for reading, as the 26/09 runs start
  halTiltSensor = HalTiltSensor{};
  halTiltSensor.begin();
}

// The same passes on every build, with every motion setting this release has left at Off
// where it has one: tilt in a book, a hard shake joining, everything off (standby), a shake
// on a plain screen (awake again), the device going to sleep, and a boot after it.
void offScenario() {
  boot();
  Settings s;
  s.screen = Settings::Reader;
  s.tilt = CrossPointTiltPageTurn::TILT_NORMAL;
  runFor(s, 2000);
  s.shake = 1;
  runFor(s, 1000);
  s = Settings{};
  runFor(s, 500);
  s.shake = 1;
  runFor(s, 1000);
  halTiltSensor.deepSleep();
  halTiltSensor = HalTiltSensor{};
  halTiltSensor.begin();
  runFor(Settings{}, 200);
}

}  // namespace

#ifdef GOLDEN_ONLY

int main() {
  offScenario();
  for (const auto& line : chip.log) std::printf("%s\n", line.c_str());
  return 0;
}

#else

namespace {

std::vector<std::string> readLines(const char* const path) {
  std::vector<std::string> lines;
  FILE* file = path ? std::fopen(path, "r") : nullptr;
  if (!file) return lines;
  char line[256];
  while (std::fgets(line, sizeof(line), file)) {
    std::string text(line);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (!text.empty() && text[0] != '#') lines.push_back(text);
  }
  std::fclose(file);
  return lines;
}

// Double tap Off: the chip sees exactly what v1.0.16 wrote and read, in the same order.
void offIsTheV1016Setup(const char* const goldenPath) {
  const auto golden = readLines(goldenPath);
  if (!expect(golden.size() > 50, "off-v1016", "the v1.0.16 trace must load")) return;
  offScenario();
  if (!expect(chip.log.size() == golden.size(), "off-v1016", "as many transactions as v1.0.16")) {
    std::printf("  now %zu, v1.0.16 %zu\n", chip.log.size(), golden.size());
  }
  for (size_t i = 0; i < std::min(chip.log.size(), golden.size()); ++i) {
    if (chip.log[i] != golden[i]) {
      std::printf("  transaction %zu: now %s, v1.0.16 %s\n", i, chip.log[i].c_str(), golden[i].c_str());
      expect(false, "off-v1016", "every transaction as v1.0.16");
      break;
    }
  }
  bool touched = false;
  for (const auto& line : chip.log) {
    touched = touched || line.rfind("W 09", 0) == 0 || line.rfind("W 0A", 0) == 0 || line.rfind("R 2F", 0) == 0 ||
              line.rfind("R 59", 0) == 0;
  }
  expect(!touched, "off-v1016", "no tap register is written or read");
}

size_t logIndex(const char* const line, const size_t from = 0) {
  for (size_t i = from; i < chip.log.size(); ++i) {
    if (chip.log[i] == line) return i;
  }
  return chip.log.size();
}

// A plain screen with shake on keeps the sensor awake; double tap joins it there.
Settings awakePlain(const uint8_t doubleTap) {
  Settings s;
  s.shake = 1;
  s.doubleTap = doubleTap;
  return s;
}

void onArmsTheEngineAsTheDatasheetSays() {
  boot();
  runFor(awakePlain(0), 500);
  const size_t before = chip.log.size();
  runFor(awakePlain(1), 20);
  const std::vector<std::string> expected = {
      "W 08=00", "W 09=80", "W 0B=07", "W 0C=00", "W 0D=12", "W 0E=00", "W 0F=50", "W 10=00", "W 12=01",
      "W 0A=0C", "R 2D x1", "W 0A=00", "R 2D x1", "W 0B=08", "W 0C=20", "W 0D=8A", "W 0E=02", "W 0F=90",
      "W 10=01", "W 12=02", "W 0A=0C", "R 2D x1", "W 0A=00", "R 2D x1", "W 03=05", "W 09=81", "W 08=03"};
  const size_t armedEnd = std::min(chip.log.size(), before + expected.size());
  const std::vector<std::string> armed(chip.log.begin() + static_cast<long>(before),
                                       chip.log.begin() + static_cast<long>(armedEnd));
  expect(armed == expected, "on-arm",
         "both parameter sets with the sensors off, then accelerometer at 224 Hz, engine on, sensors on");
  if (armed != expected) {
    for (const auto& line : armed) std::printf("  %s\n", line.c_str());
  }
  expect(chip.regs[0x04] == 0x58, "on-arm", "the gyro keeps its 28 Hz");
  expect(chip.calSet[0] && chip.calSet[1] && chip.engineOn(), "on-arm", "the chip took both sets and runs the engine");

  // Polls read the tap flag, and nothing else new.
  const size_t armedAt = chip.log.size();
  runFor(awakePlain(1), 1000);
  int status = 0;
  for (size_t i = armedAt; i < chip.log.size(); ++i) {
    status += chip.log[i] == "R 2F x1";
    expect(chip.log[i].rfind("W", 0) != 0, "on-poll", "polling writes nothing");
  }
  expect(status >= 12 && status <= 15, "on-poll", "one tap flag read per 50 ms poll after settling");
}

void offAgainPutsBackTheV1016Setup() {
  boot();
  runFor(awakePlain(1), 1000);
  const size_t before = chip.log.size();
  runFor(awakePlain(0), 20);
  const std::vector<std::string> expected = {"W 08=00", "W 09=00", "W 03=08", "W 08=03"};
  const std::vector<std::string> off(chip.log.begin() + static_cast<long>(before),
                                     chip.log.begin() + static_cast<long>(std::min(chip.log.size(), before + 4)));
  expect(off == expected, "on-off", "engine off, accelerometer back to 28 Hz, sensors on");
  expect(chip.regs[0x03] == 0x08 && chip.regs[0x04] == 0x58 && chip.regs[0x09] == 0x00 && chip.regs[0x08] == 0x03,
         "on-off", "the registers v1.0.16 leaves");
  const size_t offAt = chip.log.size();
  runFor(awakePlain(0), 1000);
  for (size_t i = offAt; i < chip.log.size(); ++i) {
    expect(chip.log[i] != "R 2F x1" && chip.log[i].rfind("W", 0) != 0, "on-off", "Off again polls as before");
  }
}

void deviceSleepLeavesNoTapSetup() {
  boot();
  runFor(awakePlain(1), 1000);
  const size_t before = chip.log.size();
  halTiltSensor.deepSleep();
  const std::vector<std::string> expected = {"W 08=00", "W 09=00", "W 03=08", "W 08=03", "W 08=00", "W 02=61"};
  const std::vector<std::string> slept(chip.log.begin() + static_cast<long>(before), chip.log.end());
  expect(slept == expected, "sleep", "tap setup undone, then the usual standby");
  // Awake again: armed again, the chip may have lost its parameters in standby.
  const size_t awake = chip.log.size();
  runFor(awakePlain(1), 100);
  expect(logIndex("W 09=81", awake) < chip.log.size(), "sleep", "waking with double tap on arms it again");
}

void aChipThatNeverAnswersIsLeftAsV1016() {
  boot();
  chip.completesCommands = false;
  runFor(awakePlain(0), 500);
  const unsigned long startMs = fakeMillis;
  const size_t before = chip.log.size();
  runFor(awakePlain(1), 1000);
  int commands = 0;
  for (size_t i = before; i < chip.log.size(); ++i) commands += chip.log[i] == "W 0A=0C";
  expect(commands == 1, "timeout", "one try until the next wake");
  expect(chip.regs[0x03] == 0x08 && chip.regs[0x09] == 0x00 && chip.regs[0x08] == 0x03, "timeout",
         "the v1.0.16 setup is back and sampling");
  expect(fakeMillis - startMs < 1100, "timeout", "the wait for the chip is bounded");
}

// A report from the engine, with the device held still or moved between.
int reportsSeen(const bool clearsOnRead, const int taps, const bool moved, const int reports = 1) {
  boot();
  chip.tapClearsOnRead = clearsOnRead;
  runFor(awakePlain(1), 1500);
  int seen = 0;
  for (int r = 0; r < reports; ++r) {
    if (moved) chip.hold(0, 0, 1000);  // turned over onto its screen
    runFor(awakePlain(1), 200);
    chip.regs[0x2F] |= 0x02;
    chip.regs[0x59] = static_cast<uint8_t>(taps);
    for (int i = 0; i < 100; ++i) {
      fakeMillis += 10;
      pass(awakePlain(1));
      seen += halTiltSensor.wasDoubleTapped();
    }
    chip.regs[0x2F] &= static_cast<uint8_t>(~0x02);
    runFor(awakePlain(1), 500);
  }
  return seen;
}

void theFlagCountsOncePerReport() {
  expect(reportsSeen(true, 2, false) == 1, "flag", "a double tap is one event");
  expect(reportsSeen(true, 2, false, 3) == 3, "flag", "three double taps are three events");
  expect(reportsSeen(false, 2, false) == 1, "flag", "a flag that stays up is still one event");
  expect(reportsSeen(false, 2, false, 3) == 3, "flag", "and each time it comes up again, one more");
  expect(reportsSeen(true, 1, false) == 0, "flag", "a single tap is no event");
  expect(reportsSeen(true, 2, true) == 0, "flag", "a double knock while the device turns over is dropped");
}

// ---- The 26/09 knock run at 224 Hz, through the model and the real HAL -----------------
std::vector<Sample> loadSamples(const char* const path) {
  std::vector<Sample> samples;
  FILE* file = path ? std::fopen(path, "r") : nullptr;
  if (!file) return samples;
  char line[160];
  while (std::fgets(line, sizeof(line), file)) {
    char segment[32];
    Sample s{};
    if (line[0] == '#' || std::sscanf(line, "%31[^,],%lu,%d,%d,%d,%d,%d", segment, &s.us, &s.ax, &s.ay, &s.az,
                                      &s.gx, &s.gy) != 7)
      continue;
    s.segment = segment;
    samples.push_back(s);
  }
  std::fclose(file);
  return samples;
}

struct Counts {
  int doubles = 0;
  int engineDoubles = 0;
  int shakes = 0, faceDown = 0, faceUp = 0, forward = 0, back = 0, up = 0, down = 0;
};

Counts replay(const std::vector<Sample>& segment, const Settings& s, const unsigned long phaseMs) {
  boot();
  chip.logging = false;
  chip.hold(segment[0].ax, segment[0].ay, segment[0].az);
  chip.feed = &segment;
  chip.startMs = 1500 + phaseMs;
  const unsigned long endMs = chip.startMs + (segment.back().us - segment[0].us) / 1000 + 1500;
  Counts c;
  while (fakeMillis < endMs) {
    fakeMillis += 10;
    chip.advanceTo(fakeMillis);
    pass(s);
    c.doubles += halTiltSensor.wasDoubleTapped();
    c.shakes += halTiltSensor.wasShaken();
    c.faceDown += halTiltSensor.wasTurnedFaceDown();
    c.faceUp += halTiltSensor.wasTurnedFaceUp();
    c.forward += halTiltSensor.wasTiltedForward();
    c.back += halTiltSensor.wasTiltedBack();
    c.up += halTiltSensor.wasTiltedUp();
    c.down += halTiltSensor.wasTiltedDown();
  }
  c.engineDoubles = chip.engineReports[2];
  chip.feed = nullptr;
  return c;
}

void knockRunReplays(const char* const path) {
  const auto all = loadSamples(path);
  if (!expect(all.size() > 90000, "knock-replay", "the 224 Hz run must load")) return;
  struct Expected {
    const char* segment;
    int doubles;
  };
  // Double taps that come through, and none from anything else. Clear pairs in the run: about
  // eight on the back at an easy pace, ten fast, seven or eight on the edge.
  constexpr Expected SEGMENTS[] = {{"go-hai-lung", 7}, {"go-hai-nhanh", 10}, {"go-hai-canh", 6},
                                   {"go-mot", 0},      {"bam-nut", 0},       {"dat-xuong", 0},
                                   {"up-ngua", 0},     {"xoay-co-tay", 0},   {"cam-doc", 0}};
  const bool verbose = std::getenv("IMU_DOUBLE_TAP_VERBOSE") != nullptr;
  for (const auto& e : SEGMENTS) {
    std::vector<Sample> segment;
    for (const auto& s : all) {
      if (s.segment == e.segment) segment.push_back(s);
    }
    if (!expect(!segment.empty(), e.segment, "segment present")) continue;
    for (const unsigned long phase : {0UL, 13UL, 27UL, 41UL}) {
      const std::string label = std::string("knock-replay ") + e.segment + " +" + std::to_string(phase) + " ms";
      // Double tap alone, on a plain screen.
      Settings tapOnly;
      tapOnly.doubleTap = 1;
      const Counts tap = replay(segment, tapOnly, phase);
      if (verbose) std::printf("%s: %d double taps (engine %d)\n", label.c_str(), tap.doubles, tap.engineDoubles);
      expect(tap.doubles == e.doubles, label, "double taps as the run holds");

      // Every other gesture on, in a book and on a menu: what they find with the accelerometer at
      // 224 Hz must be what they find at the old 28 Hz.
      for (const auto screen : {Settings::Reader, Settings::Menu}) {
        Settings every;
        every.screen = screen;
        every.tilt = CrossPointTiltPageTurn::TILT_NORMAL;
        every.rows = CrossPointTiltPageTurn::TILT_NORMAL;
        every.shake = 1;
        every.strength = 0;  // Light, the easiest to set off
        every.faceDown = 1;
        every.faceUp = 1;
        const Counts old = replay(segment, every, phase);
        every.doubleTap = 1;
        const Counts now = replay(segment, every, phase);
        const std::string where = label + (screen == Settings::Reader ? " book" : " menu");
        if (verbose) {
          std::printf("%s: 28 Hz shakes %d down %d up %d turns %d/%d rows %d/%d; "
                      "224 Hz shakes %d down %d up %d turns %d/%d rows %d/%d; doubles %d\n",
                      where.c_str(), old.shakes, old.faceDown, old.faceUp, old.forward, old.back, old.up, old.down,
                      now.shakes, now.faceDown, now.faceUp, now.forward, now.back, now.up, now.down, now.doubles);
        }
        // Face down and up as before, no shake in a run that holds none, rows as before, and the
        // side flicks within one either way: the confirming read moves a poll by 15 ms, and the
        // 28 Hz stand-in is itself a model (it finds a shake in one wrist turn).
        expect(now.faceDown == old.faceDown && now.faceUp == old.faceUp, where, "face down and face up as at 28 Hz");
        expect(now.shakes == 0, where, "no shake");
        expect(now.up == old.up && now.down == old.down, where, "rows as at 28 Hz");
        expect(std::abs(now.forward - old.forward) <= 1 && std::abs(now.back - old.back) <= 1, where,
               "side flicks within one of 28 Hz");
        expect(now.doubles == tap.doubles, where, "and double tap finds the same with them on");
        if (std::string(e.segment) == "up-ngua") {
          expect(now.faceDown == 6 && now.faceUp == 6, where, "six face downs, each turned back up");
        } else {
          expect(now.faceDown == 0 && now.faceUp == 0, where, "no face down and no face up");
        }
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  offIsTheV1016Setup(argc > 1 ? argv[1] : nullptr);
  onArmsTheEngineAsTheDatasheetSays();
  offAgainPutsBackTheV1016Setup();
  deviceSleepLeavesNoTapSetup();
  aChipThatNeverAnswersIsLeftAsV1016();
  theFlagCountsOncePerReport();
  knockRunReplays(argc > 2 ? argv[2] : nullptr);
  std::printf("imu_double_tap: %d failed assertions\n", failures);
  return failures == 0 ? 0 : 1;
}

#endif
