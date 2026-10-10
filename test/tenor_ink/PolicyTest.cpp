#include <cassert>
#include "TenorInk.h"

int main() {
  using namespace tenorink;
  assert(homeGray(true, true, true, false, true));
  assert(!homeGray(false, true, true, false, true));
  assert(!homeGray(true, false, true, false, true));
  assert(!homeGray(true, true, false, false, true));
  assert(!homeGray(true, true, true, true, true));
  assert(!homeGray(true, true, true, false, false));
  assert(!homeGray(true, true, true, false, true, 1));
  assert(cachePath("/.crosspoint/epub_1/thumb2_[HEIGHT].bmp", 254, 383, true) ==
         "/.crosspoint/epub_1/ink2_4_254x383.bmp");
  assert(cachePath("/.crosspoint/epub_1/thumb2_[HEIGHT].bmp", 254, 383, false) ==
         "/.crosspoint/epub_1/ink2_1_254x383.bmp");
  assert(cachePath("/cover.bmp", 20, 30, true) == "/cover.bmp");
}
