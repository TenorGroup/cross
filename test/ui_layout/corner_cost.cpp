// What a rounded block's corner costs on a chip without a floating-point unit. Every float
// operation in lib/GfxRenderer/ContinuousCorner.h is counted here through a float stand-in; on
// the device each one is a soft-float library call. A page turn draws the status bar's battery
// icon twice with the same few corners, and popups repaint the same corner, so a corner already
// worked out must come back without redoing the curve, and the same as when it was worked out.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
long ops = 0;
struct CountedFloat {
  float v;
  constexpr CountedFloat() : v(0) {}
  constexpr CountedFloat(const float x) : v(x) {}
  constexpr CountedFloat(const double x) : v(static_cast<float>(x)) {}
  CountedFloat(const int x) : v(static_cast<float>(x)) { ++ops; }
  explicit operator int() const {
    ++ops;
    return static_cast<int>(v);
  }
};
CountedFloat operator+(const CountedFloat a, const CountedFloat b) { return ++ops, CountedFloat(a.v + b.v); }
CountedFloat operator-(const CountedFloat a, const CountedFloat b) { return ++ops, CountedFloat(a.v - b.v); }
CountedFloat operator*(const CountedFloat a, const CountedFloat b) { return ++ops, CountedFloat(a.v * b.v); }
CountedFloat operator/(const CountedFloat a, const CountedFloat b) { return ++ops, CountedFloat(a.v / b.v); }
CountedFloat operator-(const CountedFloat a) { return CountedFloat(-a.v); }
bool operator<(const CountedFloat a, const CountedFloat b) { return ++ops, a.v < b.v; }
bool operator>(const CountedFloat a, const CountedFloat b) { return ++ops, a.v > b.v; }
bool operator<=(const CountedFloat a, const CountedFloat b) { return ++ops, a.v <= b.v; }
}  // namespace

#define float CountedFloat
#include "ContinuousCorner.h"
#undef float

namespace {
long cost(const int radius, const int budget, uint8_t* cut) {
  ops = 0;
  continuouscorner::profile(radius, budget, cut);
  return ops;
}
}  // namespace

int main() {
  uint8_t cut[continuouscorner::MAX_ROWS];
  // Worked out once: the curve, thousands of operations.
  const long first = cost(26, 60, cut);
  printf("r=26 first draw: %ld float operations\n", first);
  assert(first > 1000);
  // The same corner again, and the same corner in a bigger block (smoothing is already full
  // there, so the corner is the same): nothing left to work out.
  const long again = cost(26, 60, cut);
  const long bigger = cost(26, 200, cut);
  printf("r=26 again: %ld, in a bigger block: %ld\n", again, bigger);
  // The reader status bar's battery (a 12 px tall body of radius 5 with a 1 px outline, its
  // charge set 2 px in at radius 3), drawn twice as a page turn does.
  const int battery[][2] = {{5, 6}, {4, 5}, {3, 4}};
  long turn = 0;
  for (int pass = 0; pass < 2; ++pass)
    for (const auto& corner : battery) turn += cost(corner[0], corner[1], cut);
  long second = 0;
  for (const auto& corner : battery) second += cost(corner[0], corner[1], cut);
  printf("status bar corners, two passes: %ld, a third pass: %ld\n", turn, second);
  // Served or worked out, a corner is the same: every radius and budget the firmware can ask
  // for, twice over in two orders so entries are both found and pushed out.
  int differ = 0;
  for (int round = 0; round < 2; ++round)
    for (int r = -1; r <= continuouscorner::MAX_RADIUS + 5; ++r)
      for (int i = 0; i <= 130; ++i) {
        const int budget = round == 0 ? i : 130 - i;
        uint8_t served[continuouscorner::MAX_ROWS], worked[continuouscorner::MAX_ROWS];
        memset(served, 0xA5, sizeof(served));
        memset(worked, 0xA5, sizeof(worked));
        const int a = continuouscorner::profile(r, budget, served);
        const int b = continuouscorner::profile(r, budget, worked, continuouscorner::SMOOTHING);
        differ += a != b || memcmp(served, worked, sizeof(served)) != 0 ? 1 : 0;
      }
  printf("corners that differ from the worked-out profile: %d\n", differ);
  if (again != 0 || bigger != 0 || second != 0 || differ != 0) {
    puts("FAIL: a corner already worked out is worked out again, or served different from the curve");
    return 1;
  }
  puts("PASS: a corner is worked out once and served from then on");
}
