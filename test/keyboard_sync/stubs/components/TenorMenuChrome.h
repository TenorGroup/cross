#pragma once
namespace tenorchrome {
struct X3ScreenRect {
  int x, y, width, height;
  int bottom() const { return y + height; }
};
struct X3ScreenLayout { X3ScreenRect bodyRect, tipRect, barRect, buttonBandRect; };
inline bool x3ScreenLayoutEnabled() { return false; }
inline void drawX3ActivityPath(const GfxRenderer&, const char*) {}
inline X3ScreenLayout x3ScreenLayout(const GfxRenderer& renderer, int = 0, int = 0) {
  return {{0, 16, renderer.getScreenWidth(), renderer.getScreenHeight() - 116}, {}, {}, {}};
}
inline int tipHeight(const GfxRenderer&, const char*, int = 0) { return 0; }
inline void drawInfoInRect(const GfxRenderer&, const char*, X3ScreenRect, int = 0) {}
inline void drawTipInRect(const GfxRenderer&, const char*, X3ScreenRect, int = 0) {}
inline void drawPillRing(const GfxRenderer&, int, int, int, int, int, bool) {}
inline bool kTouchShell = false;
inline bool enabled() { return false; }
inline int headerHeight() { return 48; }
inline int contentTop() { return 30; }
inline int footBackReserve() { return 84; }
inline int footBackTop(int height) { return height - 76; }
inline int tipY(const GfxRenderer&) { return 726; }
inline bool tipShown(const char*) { return true; }
inline void drawTip(const GfxRenderer&, const char*, int = 0, int = 4) {}
inline int uglyTipTop(const GfxRenderer&, int, int = 0) { return 0; }
}
