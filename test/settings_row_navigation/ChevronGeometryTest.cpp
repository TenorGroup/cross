#include <FreeInkUI.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace freeink::ui;

struct Target : DrawTarget {
  int16_t height = 26;
  std::vector<Point> pixels;
  Rect label{};
  Size measureText(FontId, const char* s, TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(s) * 8), height};
  }
  int16_t lineHeight(FontId) const override { return height; }
  void fill(Rect r, Paint, uint8_t, uint8_t) override {
    if (r.width == 1 && r.height == 1) pixels.push_back({r.x, r.y});
  }
  void stroke(Rect, Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(Point, Point, uint8_t, Paint) override {}
  void triangle(Point, Point, Point, Paint) override {}
  void text(Rect r, const char*, TextStyle) override { label = r; }
  void bitmap(Rect, BitmapRef, BitmapMode, Paint, Rotation) override {}
};

int main(int argc, char** argv) {
  Target target;
  for (int16_t height : {26, 32, 40}) {
    target.height = height;
    for (bool rtl : {false, true}) {
      ListItem item;
      item.label = "Long settings label";
      item.value = "Current";
      ListProps props;
      props.items = &item;
      props.count = 1;
      props.action = 42;
      props.rowHeight = 60;
      props.rtl = rtl;
      props.scrollIndicator = false;
      const auto plain = measureListRow(target, nullptr, 220, props, item);
      item.opensNext = true;
      const auto next = measureListRow(target, nullptr, 220, props, item);
      const auto span = listChevronSpan(height);
      const auto width = listChevronWidth(span);
      assert(plain.labelWidth - next.labelWidth == width + props.textGap);
      target.pixels.clear();
      DeviceContext device;
      device.width = 220;
      device.height = 60;
      InputSnapshot input;
      InteractionBuffer<8> interactions;
      Frame<8> frame(target, device, input, interactions);
      list(frame, Rect{0, 0, 220, 60}, props);
      assert(!target.pixels.empty());
      for (auto pixel : target.pixels) {
        assert(pixel.x >= 0 && pixel.x < 220 && pixel.y >= 0 && pixel.y < 60);
        assert(rtl ? pixel.x < target.label.x : pixel.x >= target.label.right());
      }
      assert(interactions.count() == 1);
      if (argc > 1 && height == 32 && !rtl) {
        FILE* image = std::fopen(argv[1], "wb");
        assert(image);
        std::fprintf(image, "P5\n220 60\n255\n");
        for (int y = 0; y < 60; ++y) for (int x = 0; x < 220; ++x) {
          bool ink = false;
          for (auto pixel : target.pixels) if (pixel.x == x && pixel.y == y) ink = true;
          std::fputc(ink ? 0 : 255, image);
        }
        std::fclose(image);
      }
      item.opensNext = false;
      target.pixels.clear();
      InteractionBuffer<8> plainInteractions;
      Frame<8> plainFrame(target, device, input, plainInteractions);
      list(plainFrame, Rect{0, 0, 220, 60}, props);
      assert(target.pixels.empty());
    }
  }
  std::puts("Chevron geometry: 3 font heights x LTR/RTL, reservation, paint bounds, cue off PASS");
}
