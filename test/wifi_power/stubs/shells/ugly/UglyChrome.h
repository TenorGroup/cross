#pragma once
#include "TestPlatform.h"
namespace uglychrome {
struct Marks {
  bool selected = false, chosen = false, opensNext = false, toggle = false, toggleOn = false;
};
struct Row {
  const char* label = nullptr;
  const char* subtitle = nullptr;
  const char* value = nullptr;
  const char* heading = nullptr;
  bool locked = false;
  Marks marks;
};
template <class... Args>
void row(Args&&...) {}
}  // namespace uglychrome
