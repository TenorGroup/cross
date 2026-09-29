#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "HalTiltSensor.h"
#include "TiltPickUp.h"

// ---- Fake clock and IMU for HalTiltSensor.cpp ----

namespace {
unsigned long fakeMs = 0;
Imu::Sample fakeSample{0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f};
}  // namespace

unsigned long millis() { return fakeMs; }

namespace freeink {
bool Imu::begin() {
  begun_ = true;
  addr_ = 0x6B;
  return true;
}
bool Imu::read(Sample& out) {
  out = fakeSample;
  return true;
}
bool Imu::sleep() { return true; }
bool Imu::wake() { return true; }
}  // namespace freeink

namespace {

using TiltPickUp::Verdict;

// Gravity turned by `degrees` away from the screen facing up (az -1 g on the X3).
void pose(const double degrees, int32_t (&mg)[3]) {
  const double rad = degrees * 3.14159265358979 / 180.0;
  mg[0] = static_cast<int32_t>(std::lround(1000.0 * std::sin(rad)));
  mg[1] = 0;
  mg[2] = static_cast<int32_t>(std::lround(-1000.0 * std::cos(rad)));
}

// `g` scales the reading now: 1 is gravity alone, more is a hand lifting the device.
Verdict settleAt(const double degrees, const float rateDps, const unsigned long ageMs, const double g = 1.0) {
  int32_t start[3];
  int32_t now[3];
  pose(0, start);
  pose(degrees, now);
  for (auto& axis : now) axis = static_cast<int32_t>(std::lround(axis * g));
  return TiltPickUp::settle(start, now, rateDps, ageMs);
}

// ---- The decision on its own ----

TEST(TiltPickUpSettle, SwingingBackAwayFromTheStartIsNotRest) {
  EXPECT_EQ(settleAt(60, -300.0f, 200), Verdict::Wait);
  EXPECT_EQ(settleAt(0, -300.0f, 200), Verdict::Wait);
}

TEST(TiltPickUpSettle, RestMeansGravityAlone) {
  EXPECT_EQ(settleAt(0, 0.0f, 150, 1.09), Verdict::Flick);
  EXPECT_EQ(settleAt(0, 0.0f, 150, 0.91), Verdict::Flick);
  EXPECT_EQ(settleAt(0, 0.0f, 150, 1.11), Verdict::Wait);
  EXPECT_EQ(settleAt(0, 0.0f, 150, 0.89), Verdict::Wait);
  // A lift off a table as recorded: 1.23 g, 21 degrees from the start, not turning.
  EXPECT_EQ(settleAt(21, 5.0f, 150, 1.23), Verdict::Wait);
}

TEST(TiltPickUpSettle, RestingNearTheStartIsAFlick) {
  EXPECT_EQ(settleAt(0, 0.0f, 50), Verdict::Flick);
  EXPECT_EQ(settleAt(21, 59.0f, 300), Verdict::Flick);
  EXPECT_EQ(settleAt(23, 0.0f, 300), Verdict::Wait);
  EXPECT_EQ(settleAt(0, 60.0f, 300), Verdict::Wait);
  EXPECT_EQ(settleAt(0, -60.0f, 300), Verdict::Wait);
}

TEST(TiltPickUpSettle, LeftTurnedIsAPickUpOnceTheWaitIsOver) {
  EXPECT_EQ(settleAt(90, 0.0f, 800), Verdict::Wait);
  EXPECT_EQ(settleAt(90, 0.0f, 801), Verdict::PickUp);
  EXPECT_EQ(settleAt(65, 0.0f, 801), Verdict::PickUp);
  // Upside down is as far from the start as it gets.
  EXPECT_EQ(settleAt(180, 0.0f, 801), Verdict::PickUp);
}

TEST(TiltPickUpSettle, RestReachedAfterTheWaitIsStillAPickUp) {
  EXPECT_EQ(settleAt(0, 0.0f, 800), Verdict::Flick);
  EXPECT_EQ(settleAt(0, 0.0f, 801), Verdict::PickUp);
  EXPECT_EQ(settleAt(21, 59.0f, 801), Verdict::PickUp);
}

TEST(TiltPickUpSettle, ThresholdsAreParameters) {
  TiltPickUp::Thresholds wide;
  wide.poseCosSq64 = 37;  // 40 degrees
  int32_t start[3];
  int32_t now[3];
  pose(0, start);
  pose(30, now);
  EXPECT_EQ(TiltPickUp::settle(start, now, 0.0f, 100), Verdict::Wait);
  EXPECT_EQ(TiltPickUp::settle(start, now, 0.0f, 100, wide), Verdict::Flick);
}

// ---- Recorded X3 runs through HalTiltSensor ----

struct Sample {
  unsigned long ms;
  int ax, ay, az, gx;
};

struct Clip {
  std::string name;
  std::string expected;  // forward, back or none
  std::vector<Sample> samples;
};

std::vector<Clip> loadClips() {
  std::vector<Clip> clips;
  FILE* file = std::fopen(TILT_CLIPS_PATH, "r");
  if (!file) return clips;
  char line[128];
  while (std::fgets(line, sizeof(line), file)) {
    if (line[0] == '#') continue;
    char name[48];
    char expected[16];
    if (std::sscanf(line, "clip,%47[^,],%15s", name, expected) == 2) {
      clips.push_back({name, expected, {}});
      continue;
    }
    Sample s{};
    if (!clips.empty() && std::sscanf(line, "%lu,%d,%d,%d,%d", &s.ms, &s.ax, &s.ay, &s.az, &s.gx) == 5) {
      clips.back().samples.push_back(s);
    }
  }
  std::fclose(file);
  return clips;
}

void hold(const Sample& s) {
  fakeSample = {s.ax / 1000.0f, s.ay / 1000.0f, s.az / 1000.0f, static_cast<float>(s.gx), 0.0f, 0.0f};
}

void tick() { halTiltSensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, true); }

struct Result {
  int forward = 0;
  int back = 0;
  long delayMs = -1;  // From the poll past the trigger rate to the page turn
};

// A reader open with the device held still on the clip's first sample, then
// every sample at its own time.
Result replay(const Clip& clip) {
  halTiltSensor = HalTiltSensor{};
  halTiltSensor.begin();
  const unsigned long start = 10000;
  hold(clip.samples.front());
  for (fakeMs = start - 1000; fakeMs < start; fakeMs += 50) tick();
  Result result;
  long triggerMs = -1;
  for (const auto& s : clip.samples) {
    hold(s);
    fakeMs = start + s.ms;
    tick();
    if (triggerMs < 0 && std::abs(s.gx) > 270) triggerMs = static_cast<long>(s.ms);
    const bool forward = halTiltSensor.wasTiltedForward();
    const bool back = halTiltSensor.wasTiltedBack();
    if ((forward || back) && result.delayMs < 0) result.delayMs = static_cast<long>(s.ms) - triggerMs;
    result.forward += forward;
    result.back += back;
  }
  return result;
}

// A forward flick that swings back past the trigger rate the other way 650 ms later,
// before it has come to rest, then settles where it started: one forward turn.
TEST(TiltPickUpReplay, ASwingBackDoesNotReplaceAWaitingFlick) {
  Clip clip{"swing-back", "forward", {}};
  const auto at = [&clip](const unsigned long ms, const double degrees, const int gx) {
    int32_t mg[3];
    pose(degrees, mg);
    clip.samples.push_back({ms, mg[0], mg[1], mg[2], gx});
  };
  for (unsigned long ms = 0; ms < 500; ms += 50) at(ms, 0, 0);
  at(500, 30, 400);
  for (unsigned long ms = 550; ms < 1100; ms += 50) at(ms, 40, 100);
  at(1100, 40, 0);
  at(1150, 20, -400);
  for (unsigned long ms = 1200; ms <= 2500; ms += 50) at(ms, 0, 0);
  const Result r = replay(clip);
  EXPECT_EQ(r.forward, 1);
  EXPECT_EQ(r.back, 0);
}

// Back in the book from a menu while the device is still moving (the sensor stays awake off the
// reader, so there is no wake settling): the first polls already turn fast, then the device comes
// to rest turned away. With no still pose to go back to, nothing turns.
TEST(TiltPickUpReplay, ReturningToTheBookMidMotionWaitsForAStillPose) {
  const unsigned long start = 10000;
  fakeMs = start - 3000;
  halTiltSensor = HalTiltSensor{};
  halTiltSensor.begin();
  int turns = 0;
  const auto at = [&](const unsigned long ms, const double degrees, const int gx, const bool inReader) {
    int32_t mg[3];
    pose(degrees, mg);
    hold({ms, mg[0], mg[1], mg[2], gx});
    fakeMs = start + ms;
    halTiltSensor.update(CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, inReader);
    turns += halTiltSensor.wasTiltedForward();
    turns += halTiltSensor.wasTiltedBack();
  };
  for (long ms = -2000; ms < -1000; ms += 50) at(static_cast<unsigned long>(start + ms) - start, 0, 0, true);
  for (long ms = -1000; ms < 0; ms += 50) at(static_cast<unsigned long>(start + ms) - start, 0, 0, false);
  at(0, 20, 400, true);
  at(50, 28, 350, true);
  at(100, 34, 300, true);
  for (unsigned long ms = 150; ms <= 2000; ms += 50) at(ms, 36, 0, true);
  EXPECT_EQ(turns, 0);
}

TEST(TiltPickUpReplay, FlicksStillTurnAndPickUpsDoNot) {
  const auto clips = loadClips();
  ASSERT_EQ(clips.size(), 87u) << "missing " << TILT_CLIPS_PATH;
  int flicks = 0;
  int pickUps = 0;
  long longestDelay = 0;
  for (const auto& clip : clips) {
    const Result r = replay(clip);
    if (clip.expected == "none") {
      ++pickUps;
      EXPECT_EQ(r.forward + r.back, 0) << clip.name << ": picking the device up turned a page";
      continue;
    }
    ++flicks;
    const bool forward = clip.expected == "forward";
    EXPECT_EQ(r.forward, forward ? 1 : 0) << clip.name;
    EXPECT_EQ(r.back, forward ? 0 : 1) << clip.name;
    EXPECT_LE(r.delayMs, static_cast<long>(TiltPickUp::Thresholds{}.maxWaitMs))
        << clip.name << ": the page turned too late";
    if (r.delayMs > longestDelay) longestDelay = r.delayMs;
  }
  EXPECT_EQ(flicks, 66);
  EXPECT_EQ(pickUps, 21);
  std::printf("flicks %d, pick-ups %d, longest delay %ld ms\n", flicks, pickUps, longestDelay);
}

}  // namespace
