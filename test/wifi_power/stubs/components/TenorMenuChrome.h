#pragma once
#include "TestPlatform.h"
namespace tenorchrome {
constexpr bool kTouchShell = false;
constexpr int FOOT_BACK_X = 16;
constexpr int INFO_TOP_GAP = 10;
inline bool roundFrames() { return false; }
inline int contentTop() { return 0; }
struct ScreenRect { int bottom() const { return 700; } };
struct ScreenLayout { ScreenRect bodyRect; };
inline ScreenLayout x3ScreenLayout(...) { return {}; }
inline int tipHeight(...) { return 0; }
template <class... Args>
void drawTip(Args&&...) {}
}  // namespace tenorchrome
