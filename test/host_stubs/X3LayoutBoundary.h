#pragma once
#include <initializer_list>

namespace tenorchrome {
struct X3ScreenRect {
  int x = 0, y = 0, width = 0, height = 0;
  int bottom() const { return y + height; }
};
struct X3ScreenLayout { X3ScreenRect bodyRect, tipRect, barRect, buttonBandRect; };
constexpr bool x3ScreenLayoutEnabled() { return false; }
template <typename Renderer>
X3ScreenLayout x3ScreenLayout(const Renderer& renderer, int = 0, int = 0) {
  return {{0, 16, renderer.getScreenWidth(), renderer.getScreenHeight() - 116}, {}, {}, {}};
}
template <typename... Args> int tipHeight(Args&&...) { return 0; }
template <typename Renderer> void drawInfoInRect(const Renderer&, const char*, X3ScreenRect, int = 0) {}
template <typename Renderer> void drawTipInRect(const Renderer&, const char*, X3ScreenRect, int = 0) {}
}
namespace x3activity {
struct InfoPages {
  int page = 0, pages = 1;
  bool turn(int) { return false; }
  template <typename Renderer>
  void draw(const Renderer&, tenorchrome::X3ScreenRect, std::initializer_list<const char*>) {}
};
template <typename... Args> void drawBar(Args&&...) {}
}
