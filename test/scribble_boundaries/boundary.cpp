// Synthetic boundary geometry. The device strokes live in test/scribble.
#include "hal/Scribble.h"

#include <cmath>
#include <cstdio>
#include <vector>

int failures = 0, checks = 0;
#define CHECK(expression) do { ++checks; if (!(expression)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expression); ++failures; } } while (0)

scribble::Result replay(const std::vector<scribble::Pt>& points) {
  scribble::Scribbler input;
  uint32_t now = 1000;
  for (const auto& p : points) {
    CHECK(input.step(true, p.x, p.y, now += 10).kind == scribble::Kind::None);
  }
  const auto result = input.step(false, 0, 0, now + 10);
  CHECK(input.step(false, 0, 0, now + 20).kind == scribble::Kind::None);
  return result;
}

std::vector<scribble::Pt> arc(int finish, int returnTo = -1, int rx = 200, int ry = 200) {
  std::vector<scribble::Pt> points;
  const auto add = [&](int degrees) {
    constexpr double radians = 3.14159265358979323846 / 180;
    points.push_back({static_cast<int16_t>(240 + std::lround(rx * std::cos(degrees * radians))),
                      static_cast<int16_t>(400 + std::lround(ry * std::sin(degrees * radians))), 0});
  };
  for (int a = 0; a < finish; a += 5) add(a);
  add(finish);
  if (returnTo >= 0) {
    for (int a = finish - 5; a > returnTo; a -= 5) add(a);
    add(returnTo);
  }
  return points;
}

int main() {
  using scribble::Kind;
  // Inclusive height limit, through the actual lift-time classifier.
  for (int height : {0, 39, 40, 41, 47, 48, 49, 50}) {
    const auto result = replay({{100, 300, 0}, {300, static_cast<int16_t>(300 + height), 0},
                                {100, static_cast<int16_t>(300 + height), 0}});
    CHECK(result.kind == (height <= 48 ? Kind::Strike : Kind::Unknown));
    CHECK(result.box.y1 - result.box.y0 == height);
  }
  for (int width : {95, 96, 97}) {
    const auto result = replay({{100, 300, 0}, {static_cast<int16_t>(100 + width), 301, 0}, {100, 302, 0}});
    CHECK(result.kind == (width >= 96 ? Kind::Strike : Kind::Unknown));
  }
  // A one-way swipe remains a swipe even inside the strike height band.
  CHECK(replay({{100, 300, 0}, {200, 324, 0}, {300, 348, 0}}).kind == Kind::Swipe);
  // An incomplete arc retains the ordinary closure rule.
  CHECK(replay(arc(270)).kind == Kind::Unknown);
  CHECK(replay(arc(280)).kind == Kind::Unknown);
  CHECK(replay(arc(300)).kind == Kind::Circle);
  CHECK(replay(arc(360)).kind == Kind::Circle);
  // Net winding is a full turn in both; the return consumes the one-way margin.
  CHECK(replay(arc(419, 360)).kind == Kind::Circle);
  CHECK(replay(arc(421, 360)).kind == Kind::Unknown);
  CHECK(replay(arc(450, 360)).kind == Kind::Unknown);
  // A skinny full ring keeps its own minimum dimension.
  CHECK(replay(arc(360, -1, 40, 14)).kind == Kind::Unknown);
  CHECK(replay(arc(360, -1, 40, 15)).kind == Kind::Circle);
  std::printf("Synthetic boundary checks: %d, failures: %d\n", checks, failures);
  return failures ? 1 : 0;
}
