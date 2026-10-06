#pragma once
namespace tenorchrome {
inline bool kTouchShell = false;
inline bool enabled() { return false; }
inline int headerHeight() { return 48; }
inline int contentTop() { return 30; }
inline int footBackReserve() { return 84; }
inline int footBackTop(int height) { return height - 76; }
inline int tipY(const GfxRenderer&) { return 726; }
inline void drawTip(const GfxRenderer&, const char*, int = 0) {}
}
