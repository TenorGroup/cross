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
  // X3 gray asks the one heap floor before the strip: free 10,240 B, largest reported 8,180 B.
  assert(homeGray(false, false, true, false, true, 4, true, 10240, 8180));
  assert(!homeGray(false, false, true, false, true, 4, true, 10239, 8180));
  assert(!homeGray(false, false, true, false, true, 4, true, 10240, 8179));
  assert(!homeGray(false, false, true, true, true, 4, true, 99999, 99999));
  assert(!homeGray(false, false, true, false, true, 1, true, 99999, 99999));
  assert(cachePath("/.crosspoint/epub_1/thumb2_[HEIGHT].bmp", 254, 383, true) ==
         "/.crosspoint/epub_1/ink2_4_254x383.bmp");
  assert(cachePath("/.crosspoint/epub_1/thumb2_[HEIGHT].bmp", 254, 383, false) ==
         "/.crosspoint/epub_1/ink2_1_254x383.bmp");
  assert(cachePath("/cover.bmp", 20, 30, true) == "/cover.bmp");
}
