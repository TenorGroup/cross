#pragma once
#include "TestPlatform.h"
namespace ugly {
struct Hints {
  bool back = false, confirm = false, left = false, right = false;
};
template <class... Args>
void notePage(Args&&...) {}
}  // namespace ugly
