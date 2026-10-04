#include <gtest/gtest.h>

#include <vector>

#include "src/platform/TouchProbe.h"

namespace {
struct Frame {
  int8_t state;
  int x, y;
};

std::vector<Frame> play(touchprobe::Plan plan, const std::vector<uint32_t>& polls) {
  std::vector<Frame> out;
  for (const uint32_t now : polls) {
    int x = -1, y = -1;
    const int8_t s = touchprobe::sample(plan, now, x, y);
    out.push_back({s, x, y});
  }
  return out;
}

// GfxRenderer::tapToLogical after InputManager::normalizeTouchPoint, restated.
void toLogical(const uint16_t tx, const uint16_t ty, const int orientation, const int pw, const int ph, const int tw,
               const int th, int& x, int& y) {
  const int px = static_cast<int>(static_cast<float>(tx) / tw * pw);
  const int py = static_cast<int>(static_cast<float>(ty) / th * ph);
  switch (orientation) {
    case 0: x = ph - 1 - py; y = px; break;
    case 1: x = pw - 1 - px; y = ph - 1 - py; break;
    case 2: x = py; y = pw - 1 - px; break;
    default: x = px; y = py; break;
  }
}
}  // namespace

TEST(TouchProbe, NothingPlannedReadsTheController) {
  touchprobe::Plan idle;
  int x = 0, y = 0;
  EXPECT_EQ(touchprobe::sample(idle, 1000, x, y), -1);
}

TEST(TouchProbe, SwipeMovesAlongTheLineThenLiftsOnce) {
  const auto f = play(touchprobe::swipe(400, 300, 40, 300, 200), {1000, 1050, 1100, 1150, 1199, 1200, 1210, 1220});
  ASSERT_EQ(f.size(), 8u);
  EXPECT_EQ(f[0].state, 1); EXPECT_EQ(f[0].x, 400);
  EXPECT_EQ(f[1].x, 310);
  EXPECT_EQ(f[2].x, 220);
  EXPECT_EQ(f[3].x, 130);
  EXPECT_EQ(f[4].state, 1); EXPECT_GT(f[4].x, 40);
  EXPECT_EQ(f[5].state, 1); EXPECT_EQ(f[5].x, 40); EXPECT_EQ(f[5].y, 300);
  EXPECT_EQ(f[6].state, 0);
  EXPECT_EQ(f[7].state, -1);
  for (int i = 0; i < 6; ++i) EXPECT_EQ(f[i].y, 300);
}

TEST(TouchProbe, LatePollStillReachesTheEndBeforeLifting) {
  // The loop was busy painting through the whole swipe: start point first, then the end point, then the lift.
  const auto f = play(touchprobe::swipe(100, 700, 100, 100, 300), {900, 1500, 1501, 1502});
  EXPECT_EQ(f[0].state, 1); EXPECT_EQ(f[0].y, 700);
  EXPECT_EQ(f[1].state, 1); EXPECT_EQ(f[1].x, 100); EXPECT_EQ(f[1].y, 100);
  EXPECT_EQ(f[2].state, 0);
  EXPECT_EQ(f[3].state, -1);
}

TEST(TouchProbe, TapHoldsStillThenLifts) {
  const auto f = play(touchprobe::tap(240, 400, 600), {50, 300, 649, 650, 700});
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(f[i].state, 1);
    EXPECT_EQ(f[i].x, 240);
    EXPECT_EQ(f[i].y, 400);
  }
  EXPECT_EQ(f[4].state, 0);
}

TEST(TouchProbe, ZeroLengthTapStillPressesOnce) {
  const auto f = play(touchprobe::tap(5, 6, 0), {10, 11, 12});
  EXPECT_EQ(f[0].state, 1);
  EXPECT_EQ(f[1].state, 0);
  EXPECT_EQ(f[2].state, -1);
}

TEST(TouchProbe, AppPointRoundTripsThroughTheControllerFrame) {
  // X4 Pro: panel 800 x 480, touch frame the same size; a touch frame of another size too.
  for (const int tw : {800, 1024}) {
    const int th = tw == 800 ? 480 : 600;
    for (int o = 0; o < 4; ++o) {
      const int w = (o == 0 || o == 2) ? 480 : 800, h = (o == 0 || o == 2) ? 800 : 480;
      for (const auto& p : {std::pair{0, 0}, std::pair{w - 1, h - 1}, std::pair{240, 30}, std::pair{17, 433}}) {
        uint16_t tx = 0, ty = 0;
        touchprobe::toTouch(p.first, p.second, o, 800, 480, tw, th, tx, ty);
        int x = -1, y = -1;
        toLogical(tx, ty, o, 800, 480, tw, th, x, y);
        EXPECT_EQ(x, p.first) << "o=" << o << " tw=" << tw;
        EXPECT_EQ(y, p.second) << "o=" << o << " tw=" << tw;
      }
    }
  }
}
