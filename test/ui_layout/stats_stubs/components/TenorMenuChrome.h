#pragma once
// Host render of the button boards' Stats panels: the touch shell's panel ring is not drawn here.
class GfxRenderer;
namespace tenorchrome {
constexpr bool kTouchShell = false;
constexpr int FOOT_BACK_X = 16;
inline void drawPanel(const GfxRenderer&, int, int) {}
}  // namespace tenorchrome
