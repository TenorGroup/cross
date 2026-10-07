#include <cassert>
#include <cstdio>

#include "components/OptionPopupLayout.h"

int main() {
  assert(optionPopupFrameRows(350, 0, 56, 3) == 3);
  assert(optionPopupFrameRows(80, 0, 56, 8) == 1);
  assert(optionPopupFrameRows(350, 0, 56, 100) <= 16);
  assert(optionPopupFrameRows(350, 0, 56, 100) * 56 + 12 <= 350);
  assert(optionPopupFrameRows(350, 0, 0, 3) >= 1);
  assert(optionPopupFrameRows(350, 0, 56, 0) == 0);
  std::puts("PASS: in-frame option rows stay bounded inside the fixed parent frame");
}
