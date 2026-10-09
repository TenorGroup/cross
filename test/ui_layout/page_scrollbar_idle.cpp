#include <cassert>
#include <cstdint>

#include "components/PageScrollbarIdle.h"

int main() {
  PageScrollbarIdle idle;
  assert(!idle.isVisible());
  assert(!idle.expired(2000));
  idle.show(123);
  assert(idle.isVisible());
  assert(!idle.expired(2122));
  assert(idle.expired(2123));
  const uint32_t beforeHide = idle.generation();
  idle.show(2124);
  idle.hideIfUnchanged(beforeHide);
  assert(idle.isVisible());
  assert(!idle.expired(4123));
  assert(idle.expired(4124));
  idle.hideIfUnchanged(idle.generation());
  assert(!idle.isVisible());
  assert(!idle.expired(6000));
  idle.show(UINT32_MAX - 999);
  assert(!idle.expired(999));
  assert(idle.expired(1000));
  idle.hide();
  assert(!idle.isVisible());
}
