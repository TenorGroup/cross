#pragma once
#include "TestPlatform.h"
namespace tenorchrome {
constexpr bool kTouchShell = false;
template <class... Args>
void drawTip(Args&&...) {}
}  // namespace tenorchrome
