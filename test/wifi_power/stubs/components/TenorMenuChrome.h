#pragma once
#include "TestPlatform.h"
namespace tenorchrome {
constexpr bool kTouchShell = false;
constexpr int FOOT_BACK_X = 16;
template <class... Args>
void drawTip(Args&&...) {}
}  // namespace tenorchrome
