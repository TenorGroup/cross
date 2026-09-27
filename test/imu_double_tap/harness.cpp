// The SDK's QMI8658 driver and the tilt sensor HAL on a fake chip: every I2C transaction
// is logged, and the chip answers the way its datasheet (QMI8658C Rev A, sections 5 and 8)
// describes, FIFO included. Like the X3 measured on 26/09 it never raises its own tap flag,
// so a double tap has to come from the firmware reading the samples.
#include <Wire.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "HalTiltSensor.h"
#if __has_include("TapDetector.h")
#include "TapDetector.h"
#define HAVE_TAP_DETECTOR
#endif

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
  int gz = 0;
};

// ---- The fake QMI8658 at 0x6B ---------------------------------------------------------
constexpr uint8_t CHIP_ADDR = 0x6B;
constexpr unsigned long SAMPLE_US = 4460;  // 224.2 Hz

struct FakeChip {
  uint8_t regs[256] = {};
  uint8_t pointer = 0;
  bool completesCommands = true;
  bool logging = true;
  std::vector<std::string> log;

  // The motion fed to the chip: its samples at 224 Hz, from `startMs` on. Without a feed the
  // chip samples the held pose at the same rate.
  const std::vector<Sample>* feed = nullptr;
  size_t next = 0;
  unsigned long startMs = 0;
  unsigned long holdUs = 0;
  Sample latest{"", 0, 0, 0, -1000, 0, 0};
  // The 28 Hz output: every eighth sample, the mean of the last eight.
  Sample window[8] = {};
  int windowCount = 0;
  Sample held{"", 0, 0, 0, -1000, 0, 0};

  // FIFO (datasheet section 8): whole frames, the oldest dropped in stream mode.
  std::deque<std::vector<uint8_t>> fifo;
  std::vector<uint8_t> reading;  // What FIFO_DATA hands out in read mode
  size_t readingNext = 0;
  bool overflowed = false;
  bool mixedRates = false;  // Both sensors into the FIFO at two rates, which it does not take
  long framesTaken = 0, framesDropped = 0, framesRead = 0;

  void reset() {
    *this = FakeChip{};
    regs[0x00] = 0x05;
    regs[0x08] = 0x00;
  }

  bool accelOn() const { return regs[0x08] & 0x01; }
  bool gyroOn() const { return regs[0x08] & 0x02; }
  bool fifoOn() const { return (regs[0x14] & 0x03) != 0; }
  bool readMode() const { return regs[0x14] & 0x80; }
  size_t fifoDepth() const { return size_t{16} << ((regs[0x14] >> 2) & 0x03); }
  size_t frameBytes() const { return (accelOn() ? 6 : 0) + (gyroOn() ? 6 : 0); }
  size_t fifoBytes() const {
    size_t bytes = 0;
    for (const auto& frame : fifo) bytes += frame.size();
    return bytes;
  }

  void command(const uint8_t value) {
    if (value == 0x00) {
      regs[0x2D] &= static_cast<uint8_t>(~0x80);
      return;
    }
    if (!completesCommands) return;
    if (value == 0x04) {
      fifo.clear();
      overflowed = false;
    } else if (value == 0x05) {
      // Read mode: FIFO_DATA hands out what the FIFO holds, and new samples are dropped.
      regs[0x14] |= 0x80;
      reading.clear();
      readingNext = 0;
      for (const auto& frame : fifo) reading.insert(reading.end(), frame.begin(), frame.end());
      // An X3's QMI8658 hands out the newest frame's gz as noise near -32768 (27/09 FIFO run: the
      // last frame of every read, 283 of 283), the rest of the frame and every older one whole.
      if (reading.size() >= 12 && frameBytes() == 12) put16(&reading[reading.size() - 2], -32600);
    }
    regs[0x2D] |= 0x80;
  }

  void write(const uint8_t reg, const uint8_t value) {
    if (reg == 0x14 && readMode() && !(value & 0x80)) {
      // Out of read mode: what was read has left the FIFO.
      size_t consumed = readingNext;
      while (!fifo.empty() && consumed >= fifo.front().size()) {
        consumed -= fifo.front().size();
        fifo.pop_front();
        ++framesRead;
      }
      overflowed = false;
    }
    regs[reg] = value;
    if (reg == 0x0A) command(value);
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
      put16(&out[10], g.gz * 64);
      return out[reg - 0x35];
    }
    if (reg == 0x15 || reg == 0x16) {
      const size_t words = fifoBytes() / 2;
      if (reg == 0x15) return static_cast<uint8_t>(words & 0xFF);
      return static_cast<uint8_t>((fifo.size() >= fifoDepth() ? 0x80 : 0) | (overflowed ? 0x20 : 0) |
                                  (fifo.empty() ? 0 : 0x10) | ((words >> 8) & 0x03));
    }
    if (reg == 0x17) return readMode() && readingNext < reading.size() ? reading[readingNext++] : 0;
    return regs[reg];
  }

  // One sample of the sensors: the data registers, the 28 Hz stand-in and the FIFO.
  void take(const Sample& s) {
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
        mean.gz += w.gz;
      }
      mean.ax /= 8;
      mean.ay /= 8;
      mean.az /= 8;
      mean.gx /= 8;
      mean.gy /= 8;
      mean.gz /= 8;
      held = mean;
    }
    if (!fifoOn() || frameBytes() == 0) return;
    if (accelOn() && gyroOn() && (regs[0x03] & 0x0F) != (regs[0x04] & 0x0F)) {
      mixedRates = true;
      return;
    }
    if (readMode()) {
      ++framesDropped;
      return;
    }
    std::vector<uint8_t> frame(frameBytes());
    size_t at = 0;
    if (accelOn()) {
      put16(&frame[0], s.ax * 16384 / 1000);
      put16(&frame[2], s.ay * 16384 / 1000);
      put16(&frame[4], s.az * 16384 / 1000);
      at = 6;
    }
    if (gyroOn()) {
      put16(&frame[at], s.gx * 64);
      put16(&frame[at + 2], s.gy * 64);
      put16(&frame[at + 4], s.gz * 64);
    }
    if (fifo.size() >= fifoDepth()) {
      if ((regs[0x14] & 0x03) == 0x01) {
        ++framesDropped;  // FIFO mode keeps the old ones
        return;
      }
      fifo.pop_front();
      ++framesDropped;
      overflowed = true;
    }
    fifo.push_back(frame);
    ++framesTaken;
  }

  // Plays the samples up to `ms`.
  void advanceTo(const unsigned long ms) {
    if (!feed) {
      while (holdUs + SAMPLE_US <= ms * 1000UL) {
        holdUs += SAMPLE_US;
        Sample s = latest;
        s.us = holdUs;
        take(s);
      }
      return;
    }
    while (next < feed->size()) {
      const Sample& s = (*feed)[next];
      const unsigned long at = startMs + ((s.us - (*feed)[0].us) / 1000);
      if (at > ms) break;
      take(s);
      ++next;
    }
    holdUs = ms * 1000UL;
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
  // FIFO_DATA hands out the next byte on every read; other registers step on.
  for (uint8_t i = 0; i < len; ++i) {
    rxBuffer.push_back(chip.read(chip.pointer == 0x17 ? 0x17 : static_cast<uint8_t>(chip.pointer + i)));
  }
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
  uint8_t screenTap = 0;
  uint8_t edgeTap = 0;
  enum Screen { Plain, Reader, Menu } screen = Plain;
};

void pass(const Settings& s) {
  halTiltSensor.setStrength(1, 0);
  halTiltSensor.configureShake(s.shake, s.strength);
#ifdef HAVE_FLIP
  halTiltSensor.configureFlip(s.faceDown, s.faceUp);
#endif
#ifdef HAVE_DOUBLE_TAP
  halTiltSensor.configureDoubleTap(s.doubleTap, s.screenTap, s.edgeTap);
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

bool isFifoOrTap(const std::string& line) {
  for (const char* const prefix : {"W 09", "W 0A", "R 2F", "R 59", "W 13", "W 14", "R 15", "R 16", "R 17"}) {
    if (line.rfind(prefix, 0) == 0) return true;
  }
  return false;
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
  for (const auto& line : chip.log) touched = touched || isFifoOrTap(line);
  expect(!touched, "off-v1016", "no FIFO or tap register is written or read");
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

void onStreamsBothSensorsIntoTheFifo() {
  boot();
  runFor(awakePlain(0), 500);
  const size_t before = chip.log.size();
  runFor(awakePlain(1), 20);
  // Sensors off, the CTRL9 done flag read from STATUSINT, the FIFO emptied and set to stream
  // 128 frames, both sensors at 224 Hz (the FIFO takes two sensors only at one rate), on.
  const std::vector<std::string> expected = {"W 08=00", "W 09=80", "W 0A=04", "R 2D x1", "W 0A=00", "R 2D x1",
                                             "W 14=0E", "W 03=05", "W 04=55", "W 08=03"};
  const size_t armedEnd = std::min(chip.log.size(), before + expected.size());
  const std::vector<std::string> armed(chip.log.begin() + static_cast<long>(before),
                                       chip.log.begin() + static_cast<long>(armedEnd));
  expect(armed == expected, "on-arm", "FIFO reset and set to stream with the sensors off, then both at 224 Hz, on");
  if (armed != expected) {
    for (const auto& line : armed) std::printf("  %s\n", line.c_str());
  }

  // Each poll empties the FIFO: count, read mode through CTRL9, whole frames at most eight at
  // a time, write mode again. Nothing else, and no frame lost or read twice.
  const size_t armedAt = chip.log.size();
  runFor(awakePlain(1), 2000);
  int counts = 0, requests = 0, backToWrite = 0;
  bool chunksWhole = true, registersRead = false;
  for (size_t i = armedAt; i < chip.log.size(); ++i) {
    const std::string& line = chip.log[i];
    counts += line == "R 15 x2";
    requests += line == "W 0A=05";
    backToWrite += line == "W 14=0E";
    if (line.rfind("R 17 x", 0) == 0) {
      const int bytes = std::atoi(line.c_str() + 6);
      chunksWhole = chunksWhole && bytes > 0 && bytes <= 96 && bytes % 12 == 0;
    }
    registersRead = registersRead || line.rfind("R 35", 0) == 0 || line.rfind("R 3B", 0) == 0;
    expect(line.rfind("W", 0) != 0 || line == "W 0A=05" || line == "W 0A=00" || line == "W 14=0E", "on-poll",
           "polling writes only the FIFO read handshake");
  }
  expect(counts >= 30 && counts <= 42, "on-poll", "one FIFO count per 50 ms poll after settling");
  expect(requests == backToWrite && requests >= 30, "on-poll", "every read mode is ended");
  expect(chunksWhole, "on-poll", "whole frames, at most eight per I2C read");
  expect(!registersRead, "on-poll", "the samples come from the FIFO, not the data registers");
  expect(!chip.mixedRates, "on-poll", "both sensors at one rate while they fill the FIFO");
  expect(chip.framesDropped == 0, "on-poll", "no frame dropped at the poll rate");
  expect(chip.framesRead + static_cast<long>(chip.fifo.size()) == chip.framesTaken, "on-poll",
         "every frame taken is read once or still waits");
}

void offAgainPutsBackTheV1016Setup() {
  boot();
  runFor(awakePlain(1), 1000);
  const size_t before = chip.log.size();
  runFor(awakePlain(0), 20);
  const std::vector<std::string> expected = {"W 08=00", "W 14=00", "W 09=00", "W 03=08", "W 04=58", "W 08=03"};
  const std::vector<std::string> off(
      chip.log.begin() + static_cast<long>(before),
      chip.log.begin() + static_cast<long>(std::min(chip.log.size(), before + expected.size())));
  expect(off == expected, "on-off", "FIFO off, both sensors back to 28 Hz, on");
  if (off != expected) {
    for (const auto& line : off) std::printf("  %s\n", line.c_str());
  }
  expect(chip.regs[0x03] == 0x08 && chip.regs[0x04] == 0x58 && chip.regs[0x09] == 0x00 && chip.regs[0x08] == 0x03 &&
             chip.regs[0x14] == 0x00,
         "on-off", "the registers v1.0.16 leaves");
  const size_t offAt = chip.log.size();
  runFor(awakePlain(0), 1000);
  for (size_t i = offAt; i < chip.log.size(); ++i) {
    expect(!isFifoOrTap(chip.log[i]) && chip.log[i].rfind("W", 0) != 0, "on-off", "Off again polls as before");
  }
}

void deviceSleepLeavesNoFifoSetup() {
  boot();
  runFor(awakePlain(1), 1000);
  const size_t before = chip.log.size();
  halTiltSensor.deepSleep();
  const std::vector<std::string> expected = {"W 08=00", "W 14=00", "W 09=00", "W 03=08",
                                             "W 04=58", "W 08=03", "W 08=00", "W 02=61"};
  const std::vector<std::string> slept(chip.log.begin() + static_cast<long>(before), chip.log.end());
  expect(slept == expected, "sleep", "FIFO setup undone, then the usual standby");
  // Awake again: set up again, the chip may have lost it in standby.
  const size_t awake = chip.log.size();
  runFor(awakePlain(1), 100);
  expect(logIndex("W 14=0E", awake) < chip.log.size(), "sleep", "waking with double tap on sets the FIFO up again");
}

// X3 26/09-27/09: waking from deep sleep with double tap on, the first arm, 20 ms after the
// sensor left power-down, timed out every time (3/3), and double tap stayed dead until it was
// turned off and on. A chip that answers later is armed on a later try.
void aLateChipIsArmedOnALaterTry() {
  boot();
  chip.completesCommands = false;
  runFor(awakePlain(1), 500);
  chip.completesCommands = true;
  const size_t before = chip.log.size();
  runFor(awakePlain(1), 3000);
  expect(logIndex("W 14=0E", before) < chip.log.size() && chip.regs[0x14] == 0x0E, "late",
         "armed once the chip answers, without turning double tap off and on");
}

void aChipThatNeverAnswersIsLeftAsV1016() {
  boot();
  chip.completesCommands = false;
  runFor(awakePlain(0), 500);
  const unsigned long startMs = fakeMillis;
  const size_t before = chip.log.size();
  runFor(awakePlain(1), 10000);
  int commands = 0;
  for (size_t i = before; i < chip.log.size(); ++i) commands += chip.log[i] == "W 0A=04";
  expect(commands == 3, "timeout", "three tries until the next wake");
  expect(chip.regs[0x03] == 0x08 && chip.regs[0x04] == 0x58 && chip.regs[0x09] == 0x00 && chip.regs[0x08] == 0x03 &&
             chip.regs[0x14] == 0x00,
         "timeout", "the v1.0.16 setup is back and sampling");
  expect(fakeMillis - startMs < 10200, "timeout", "the wait for the chip is bounded");
}

// ---- Knocks made up sample by sample ----------------------------------------------------
// The device lies at `pose` from `fromUs`; each knock is three samples 1200 mg as a knock pushes
// it: on the back toward -z, on the screen toward +z, on a side edge along the screen's long side.
enum class Knock { Back, Screen, Edge };
std::vector<Sample> knocks(const int (&pose)[3], const std::vector<unsigned long>& knockMs, const unsigned long totalMs,
                           const int (&before)[3], const unsigned long turnMs, const Knock where = Knock::Back) {
  std::vector<Sample> out;
  for (unsigned long us = 0; us < totalMs * 1000UL; us += SAMPLE_US) {
    const bool turned = us >= turnMs * 1000UL;
    Sample s{"made", us, turned ? pose[0] : before[0], turned ? pose[1] : before[1], turned ? pose[2] : before[2], 0, 0};
    for (const unsigned long k : knockMs) {
      if (us >= k * 1000UL && us < k * 1000UL + 3 * SAMPLE_US) {
        (where == Knock::Edge ? s.ay : s.az) += where == Knock::Back ? -1200 : 1200;
      }
    }
    out.push_back(s);
  }
  return out;
}

struct Taps {
  int back = 0, screen = 0, edge = 0;
};

Taps tapsIn(const std::vector<Sample>& feed, const uint8_t backAction, const uint8_t screenAction,
            const uint8_t edgeAction) {
  boot();
  chip.logging = false;
  chip.hold(feed[0].ax, feed[0].ay, feed[0].az);
  chip.feed = &feed;
  chip.startMs = 0;
  Taps seen;
  Settings s = awakePlain(backAction);
  s.screenTap = screenAction;
  s.edgeTap = edgeAction;
  const unsigned long endMs = (feed.back().us / 1000) + 500;
  while (fakeMillis < endMs) {
    fakeMillis += 10;
    chip.advanceTo(fakeMillis);
    pass(s);
    seen.back += halTiltSensor.wasDoubleTapped();
    seen.screen += halTiltSensor.wasScreenTapped();
    seen.edge += halTiltSensor.wasEdgeTapped();
  }
  chip.feed = nullptr;
  return seen;
}

int doubleTapsIn(const std::vector<Sample>& feed) { return tapsIn(feed, 1, 0, 0).back; }

void madeUpKnocks() {
  const int still[3] = {-850, -100, -480};
  const int over[3] = {0, 0, 1000};  // turned over onto its screen
  expect(doubleTapsIn(knocks(still, {2000, 2200}, 3500, still, 0)) == 1, "made", "two knocks 200 ms apart are one");
  expect(doubleTapsIn(knocks(still, {1500, 1700, 2600, 2760, 3700, 3930}, 5000, still, 0)) == 3, "made",
         "three pairs are three double taps");
  expect(doubleTapsIn(knocks(still, {2000}, 3500, still, 0)) == 0, "made", "one knock is no double tap");
  // The back, the screen and a side edge are three gestures: each comes out only as itself, and
  // only when on.
  const auto back = knocks(still, {2000, 2200}, 3500, still, 0);
  const auto screen = knocks(still, {2000, 2200}, 3500, still, 0, Knock::Screen);
  const auto edge = knocks(still, {2000, 2200}, 3500, still, 0, Knock::Edge);
  Taps t = tapsIn(back, 1, 1, 1);
  expect(t.back == 1 && t.screen == 0 && t.edge == 0, "made-place", "two knocks into the back are a back double tap");
  t = tapsIn(screen, 1, 1, 1);
  expect(t.back == 0 && t.screen == 1 && t.edge == 0, "made-place", "two knocks on the screen are a screen double tap");
  t = tapsIn(edge, 1, 1, 1);
  expect(t.back == 0 && t.screen == 0 && t.edge == 1, "made-place", "two knocks on the edge are an edge double tap");
  t = tapsIn(edge, 1, 1, 0);
  expect(t.back + t.screen + t.edge == 0, "made-place", "an edge double tap with the edge Off runs nothing");
  t = tapsIn(screen, 1, 0, 1);
  expect(t.back + t.screen + t.edge == 0, "made-place", "a screen double tap with the screen Off runs nothing");
  t = tapsIn(back, 0, 1, 1);
  expect(t.back + t.screen + t.edge == 0, "made-place", "a back double tap with the back Off runs nothing");
  expect(doubleTapsIn(knocks(still, {2000, 2500}, 3500, still, 0)) == 0, "made", "500 ms apart is too slow");
  expect(doubleTapsIn(knocks(over, {2300, 2500}, 3500, still, 2000)) == 0, "made",
         "a double knock while the device turns over is dropped");
}

// A knock read just before the loop stalls, and one 500 ms later found after it in a FIFO that
// dropped the frames between: the detector starts over rather than count them as a pair.
void aStallIsAGap() {
  const int still[3] = {-850, -100, -480};
  const auto feed = knocks(still, {2000, 2500}, 4000, still, 0);
  boot();
  chip.logging = false;
  chip.hold(feed[0].ax, feed[0].ay, feed[0].az);
  chip.feed = &feed;
  int seen = 0;
  while (fakeMillis < 4500) {
    fakeMillis += 10;
    chip.advanceTo(fakeMillis);
    // The loop is busy from 40 ms after the first knock until 900 ms after it (a long paint).
    if (fakeMillis > 2040 && fakeMillis < 2900) continue;
    pass(awakePlain(1));
    seen += halTiltSensor.wasDoubleTapped();
  }
  chip.feed = nullptr;
  expect(chip.framesDropped > 0, "stall", "the FIFO filled up and dropped frames");
  expect(seen == 0, "stall", "knocks with lost frames between are no double tap");
}

// ---- The 26/09 knock run at 224 Hz, through the firmware's detector and the real HAL -------
std::vector<Sample> loadSamples(const char* const path) {
  std::vector<Sample> samples;
  FILE* file = path ? std::fopen(path, "r") : nullptr;
  if (!file) return samples;
  char line[192];
  while (std::fgets(line, sizeof(line), file)) {
    char segment[32];
    Sample s{};
    if (line[0] == '#' || std::sscanf(line, "%31[^,],%lu,%d,%d,%d,%d,%d,%d", segment, &s.us, &s.ax, &s.ay, &s.az,
                                      &s.gx, &s.gy, &s.gz) != 8)
      continue;
    s.segment = segment;
    samples.push_back(s);
  }
  std::fclose(file);
  return samples;
}

struct Counts {
  int doubles = 0, screens = 0, edges = 0;
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
    c.screens += halTiltSensor.wasScreenTapped();
    c.edges += halTiltSensor.wasEdgeTapped();
    c.shakes += halTiltSensor.wasShaken();
    c.faceDown += halTiltSensor.wasTurnedFaceDown();
    c.faceUp += halTiltSensor.wasTurnedFaceUp();
    c.forward += halTiltSensor.wasTiltedForward();
    c.back += halTiltSensor.wasTiltedBack();
    c.up += halTiltSensor.wasTiltedUp();
    c.down += halTiltSensor.wasTiltedDown();
  }
  chip.feed = nullptr;
  return c;
}

struct Expected {
  const char* segment;
  int backs;     // Through the HAL: double taps on the back
  int screens;   // ... on the screen
  int edges;     // ... and on a side edge
  int detector;  // The detector alone, before the pose check and the direction
};

// `compareGestures`: also replay every other gesture at 28 Hz and through the FIFO. Their match
// was tuned on the 26/09 run; the 27/09 run is for the taps only.
void knockRunReplays(const char* const path, const std::vector<Expected>& SEGMENTS, const bool compareGestures) {
  const auto all = loadSamples(path);
  if (!expect(all.size() > 15000, "knock-replay", "the run must load")) return;
  const bool verbose = std::getenv("IMU_DOUBLE_TAP_VERBOSE") != nullptr;
  for (const auto& e : SEGMENTS) {
    std::vector<Sample> segment;
    for (const auto& s : all) {
      if (s.segment == e.segment) segment.push_back(s);
    }
    if (!expect(!segment.empty(), e.segment, "segment present")) continue;
#ifdef HAVE_TAP_DETECTOR
    TapDetector detector;
    int found = 0;
    for (const auto& s : segment) {
      found += detector.step(s.ax, s.ay, s.az, std::max({std::abs(s.gx), std::abs(s.gy), std::abs(s.gz)})) == 2;
    }
    if (verbose) std::printf("%s: detector alone %d double taps\n", e.segment, found);
    expect(found == e.detector, std::string("detector ") + e.segment, "double taps the detector finds on its own");
#endif
    for (const unsigned long phase : {0UL, 13UL, 27UL, 41UL}) {
      const std::string label = std::string("knock-replay ") + e.segment + " +" + std::to_string(phase) + " ms";
      // Double tap alone, both gestures on, on a plain screen.
      Settings tapOnly;
      tapOnly.doubleTap = 1;
      tapOnly.screenTap = 1;
      tapOnly.edgeTap = 1;
      const Counts tap = replay(segment, tapOnly, phase);
      if (verbose) {
        std::printf("%s: %d on the back, %d on the screen, %d on an edge\n", label.c_str(), tap.doubles, tap.screens,
                    tap.edges);
      }
      expect(tap.doubles == e.backs, label, "double taps on the back as the run holds");
      expect(tap.screens == e.screens, label, "double taps on the screen as the run holds");
      expect(tap.edges == e.edges, label, "double taps on an edge as the run holds");

      // Every other gesture on, in a book and on a menu: what they find while the FIFO runs
      // must be what they find at the old 28 Hz.
      for (const auto screen : {Settings::Reader, Settings::Menu}) {
        if (!compareGestures) break;
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
        every.screenTap = 1;
        every.edgeTap = 1;
        const Counts now = replay(segment, every, phase);
        const std::string where = label + (screen == Settings::Reader ? " book" : " menu");
        if (verbose) {
          std::printf("%s: 28 Hz shakes %d down %d up %d turns %d/%d rows %d/%d; "
                      "FIFO shakes %d down %d up %d turns %d/%d rows %d/%d; doubles %d\n",
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
        expect(now.doubles == tap.doubles && now.screens == tap.screens && now.edges == tap.edges, where,
               "and double tap finds the same with them on");
        if (std::string(e.segment) == "up-ngua") {
          expect(now.faceDown == 6 && now.faceUp == 6, where, "six face downs, each turned back up");
        } else {
          expect(now.faceDown == 0 && now.faceUp == 0, where, "no face down and no face up");
        }
      }
    }
  }
}

// IMU_DOUBLE_TAP_TABLE: one line per segment of a run, in file order, through the HAL with every
// double tap on: "<segment> <back> <screen> <edge> <detector>". For choosing the thresholds.
void printTable(const char* const path) {
  const auto all = loadSamples(path);
  std::vector<std::string> order;
  for (const auto& s : all) {
    if (order.empty() || order.back() != s.segment) order.push_back(s.segment);
  }
  for (const auto& name : order) {
    std::vector<Sample> segment;
    for (const auto& s : all) {
      if (s.segment == name) segment.push_back(s);
    }
    TapDetector detector;
    int found = 0;
    for (const auto& s : segment) {
      found += detector.step(s.ax, s.ay, s.az, std::max({std::abs(s.gx), std::abs(s.gy), std::abs(s.gz)})) == 2;
    }
    Settings on;
    on.doubleTap = on.screenTap = on.edgeTap = 1;
    const Counts c = replay(segment, on, 0);
    std::printf("TABLE %s %d %d %d %d\n", name.c_str(), c.doubles, c.screens, c.edges, found);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (std::getenv("IMU_DOUBLE_TAP_TABLE")) {
    for (int i = 2; i < argc; ++i) printTable(argv[i]);
    return 0;
  }
  offIsTheV1016Setup(argc > 1 ? argv[1] : nullptr);
  onStreamsBothSensorsIntoTheFifo();
  offAgainPutsBackTheV1016Setup();
  deviceSleepLeavesNoFifoSetup();
  aLateChipIsArmedOnALaterTry();
  aChipThatNeverAnswersIsLeftAsV1016();
  madeUpKnocks();
  aStallIsAGap();
  // Expected per segment, through the HAL with every double tap on: on the back, on the screen,
  // on an edge, and the detector's own count before the pose check. A finger reaching round the
  // back (tro, traigo, phaigo) and the top edge (tren) push through the back and read as it.
  // 26/09: the back at an easy pace and fast, an edge, and the controls.
  knockRunReplays(argc > 2 ? argv[2] : nullptr, {
      {"go-hai-lung", 7, 0, 0, 7}, {"go-hai-nhanh", 10, 0, 0, 10}, {"go-mot", 0, 0, 0, 0},
      {"go-hai-canh", 0, 0, 7, 7}, {"bam-nut", 0, 0, 0, 0}, {"dat-xuong", 0, 0, 0, 0},
      {"up-ngua", 0, 0, 0, 0}, {"xoay-co-tay", 0, 0, 0, 0}, {"cam-doc", 0, 0, 0, 0}}, true);
  // 27/09: held in the right, then the left hand.
  knockRunReplays(argc > 3 ? argv[3] : nullptr, {
      {"phai-camphai-cai", 0, 0, 9, 9}, {"trai-camphai-tro", 3, 0, 0, 3}, {"lung-camphai-ngon", 9, 0, 0, 9},
      {"phai-camphai-traigo", 0, 0, 0, 0}, {"bamnut-camphai", 0, 0, 0, 0}, {"bamnut2-camphai", 0, 0, 0, 0},
      {"trai-camtrai-cai", 0, 0, 10, 10}, {"phai-camtrai-tro", 5, 0, 0, 5},
      {"lung-camtrai-ngon", 9, 0, 0, 9}, {"trai-camtrai-phaigo", 0, 0, 1, 1}, {"bamnut-camtrai", 0, 0, 0, 0},
      {"bamnut2-camtrai", 0, 0, 0, 0}, {"tren-go", 8, 0, 0, 8}, {"datxuong", 0, 0, 0, 0},
      {"cam-doc", 0, 0, 0, 0}}, false);
  // 27/09: switching hands, the other hand from outside, landscape.
  knockRunReplays(argc > 4 ? argv[4] : nullptr, {
      {"doitay", 0, 0, 0, 0}, {"phai-camtrai-ngoai", 0, 1, 7, 8}, {"trai-camphai-ngoai", 0, 0, 2, 2},
      {"ngang-phai-cai", 0, 0, 7, 7}, {"ngang-trai-cai", 0, 0, 5, 5}, {"ngang-lung", 7, 0, 0, 7},
      {"ngang-doc", 0, 0, 0, 0}}, false);
  // 27/09: the screen; light taps, landscape and a table give nothing.
  knockRunReplays(argc > 5 ? argv[5] : nullptr, {
      {"manhinh-camphai-traigo", 0, 4, 0, 4}, {"manhinh-camphai-cai", 0, 3, 0, 3},
      {"manhinh-camtrai-phaigo", 0, 4, 0, 4}, {"manhinh-camtrai-cai", 0, 6, 0, 6},
      {"manhinh-nhe", 0, 0, 0, 0}, {"ngang-manhinh", 0, 0, 0, 0}, {"ban-manhinh", 0, 0, 0, 0},
      {"ban-canh", 0, 0, 0, 0}, {"cham-manhinh", 0, 0, 0, 0}}, false);
  // 27/09: the device's own FIFO frames, 186 Hz, with the chip's broken gz. The screen's first tap
  // lands under the peak (about 460 mg) and the edge's pairs are missed: both stay experimental.
  knockRunReplays(argc > 6 ? argv[6] : nullptr, {
      {"lung-1", 3, 0, 0, 0}, {"manhinh-1", 0, 0, 0, 0}, {"canh-1", 0, 0, 0, 0},
      {"lung-camphai", 6, 0, 0, 4}}, false);
  std::printf("imu_double_tap: %d failed assertions\n", failures);
  return failures == 0 ? 0 : 1;
}

#endif
