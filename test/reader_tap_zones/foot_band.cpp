// The band under the X4 Pro's dynamic bar (v1.0.75): a tap there lands on the bar's bottom row while a frame with
// the bar is on the panel; nothing moves without a bar or under the jump bar (top 0).
#include <cassert>

#include "components/HeaderBackTapTarget.h"

int main() {
  using namespace HeaderBackTapTarget;
  setFootBand(784, 800);  // the bar's 724..783, 16 px over the edge of a 800 px portrait screen
  assert(footTapY(784) == 783);
  assert(footTapY(799) == 783);
  assert(footTapY(783) == 783);
  assert(footTapY(754) == 754);
  assert(footTapY(800) == 800);
  setFootBand(464, 480);  // landscape: the bar's 404..463
  assert(footTapY(470) == 463);
  assert(footTapY(790) == 790);
  setFootBand(0, 800);  // no bar, or the jump bar's 3 seconds: the band is the page's
  assert(footTapY(792) == 792);
  return 0;
}
