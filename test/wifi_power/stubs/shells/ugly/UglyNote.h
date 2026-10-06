#pragma once
#include "TestPlatform.h"
namespace ugly {
struct Hints {
  bool back = false, confirm = false, left = false, right = false;
};
struct Box {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
enum class Size { S22, S30, S38, S52 };
inline constexpr int NOTE_X = 48;
template <class... Args>
void notePage(Args&&...) {}
template <class... Args>
int paragraph(Args&&...) {
  return 1;
}
}  // namespace ugly
