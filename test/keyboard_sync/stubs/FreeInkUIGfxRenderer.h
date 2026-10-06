#pragma once
#include <FreeInkUI.h>
#include <GfxRenderer.h>

namespace freeink::ui {
class GfxRendererTarget : public DrawTarget {
 public:
  static constexpr FontId FONT_SMALL = 0, FONT_BODY = 1;
  explicit GfxRendererTarget(const GfxRenderer&) {}
  void setFont(FontId, int) {}
  void setPaintingEnabled(bool) {}
  DeviceContext deviceContext() const {
    DeviceContext device;
    device.width = 528;
    device.height = 792;
    return device;
  }
  Size measureText(FontId, const char* text, TextStyle) const override { return {static_cast<int16_t>(strlen(text) * 8), 20}; }
  int16_t lineHeight(FontId) const override { return 20; }
  void fill(Rect, Paint, uint8_t = 0, uint8_t = CornersAll) override {}
  void stroke(Rect, Paint, uint8_t, uint8_t = 0, uint8_t = CornersAll) override {}
  void line(Point, Point, uint8_t, Paint) override {}
  void triangle(Point, Point, Point, Paint) override {}
  void text(Rect, const char*, TextStyle) override {}
  void bitmap(Rect, BitmapRef, BitmapMode, Paint = Paint::solid(Color::Black), Rotation = Rotation::None) override {}
};
}
