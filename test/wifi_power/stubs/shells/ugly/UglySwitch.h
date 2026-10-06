#pragma once
#include "UglyNote.h"
namespace ugly {
template <class... Args>
Box askBox(Args&&...) {
  return {};
}
}  // namespace ugly
