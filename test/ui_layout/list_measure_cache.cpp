#include "RecordingTarget.h"
#include <cstdio>
#include <cstring>
struct BoldTarget : Target {
  BoldTarget() : Target(0) {}
  fui::Size measureText(fui::FontId font, const char* text, fui::TextStyle style) const override {
    auto size = Target::measureText(font, text, style);
    if (style.bold) size.width += 20;
    return size;
  }
};
struct WrapTarget : Target {
  mutable int labelCalls = 0;
  WrapTarget() : Target(2) {}
  fui::Size measureText(fui::FontId font, const char* text, fui::TextStyle style) const override {
    if (font == 1 && style.maxLines == 3) ++labelCalls;
    return Target::measureText(font, text, style);
  }
};
static std::vector<fui::Rect> rowRuns(bool unique, int variant, int16_t width) {
  Target target(0);
  fui::ListItem items[2];
  items[0].label = "abcdefgh abcdefgh abcdefgh abcdefgh";
  items[1].label = unique ? "ijklmnop ijklmnop ijklmnop ijklmnop" : items[0].label;
  items[0].opensNext = items[1].opensNext = false;
  static const uint8_t bits[512]{};
  fui::BitmapRef marks[2]{};
  marks[1] = fui::BitmapRef{bits, 64, 24};
  if (variant == 0) items[1].icon = marks[1];
  if (variant == 1) items[1].opensNext = true;
  if (variant == 2) {
    static fui::BitmapRef small[2]{{}, fui::BitmapRef{bits, 8, 24}};
    items[0].valueIcons = small;
    items[1].valueIcons = marks;
  }
  fui::DeviceContext device; device.width = 528; device.height = 792;
  fui::InteractionBuffer<32> interactions; fui::InputSnapshot input;
  fui::Frame<32> frame(target, device, input, interactions);
  fui::ListProps props; props.items = items; props.count = 2; props.rowHeight = 36;
  props.rowPaddingY = 4; props.rowGap = 4; props.labelText.maxLines = 3;
  fui::list(frame, {0, 0, width, 700}, props);
  return target.runs;
}
int main() {
  int errors = 0;
  for (int variant = 0; variant < 3; ++variant) for (int16_t width = 120; width <= 400; width += 4) {
    auto cached = rowRuns(false, variant, width), distinct = rowRuns(true, variant, width);
    bool equal = cached.size() == distinct.size();
    for (size_t i = 0; equal && i < cached.size(); ++i)
      equal = cached[i].x == distinct[i].x && cached[i].y == distinct[i].y &&
              cached[i].width == distinct[i].width && cached[i].height == distinct[i].height;
    if (!equal) { std::printf("FAIL row geometry variant=%d width=%d\n", variant, width); ++errors; }
  }
  BoldTarget bold;
  fui::ListProps hug; hug.hugContents = true;
  fui::ListItem chosen; chosen.label = "Chosen"; chosen.chosen = true;
  const auto layout = fui::measureListRow(bold, nullptr, 300, hug, chosen);
  if (layout.naturalLabelWidth != bold.measureText(hug.labelText.font, chosen.label, hug.labelText).width) {
    std::puts("FAIL chosen hug width"); ++errors;
  }
  Target target(2);
  const char* longText="Một nhãn rất dài để buộc dàn chữ trong nhiều cột và kiểm tra việc nhớ kết quả";
  fui::ListItem items[10];
  for (auto& item:items) { item.label=longText; item.value="Giá trị dài"; item.subtitle="Mô tả phụ dài"; item.opensNext=false; }
  fui::DeviceContext device; device.width=528; device.height=792;
  fui::InteractionBuffer<32> interactions; fui::InputSnapshot input;
  fui::Frame<32> frame(target,device,input,interactions);
  fui::ListProps props; props.items=items; props.count=10; props.rowHeight=36; props.rowGap=4;
  props.labelText.font=1; props.labelText.maxLines=3; props.valueText.font=1; props.valueText.maxLines=2;
  props.subtitleText.font=0; props.subtitleText.maxLines=2; props.partialTrailingRow=true; props.partialTrailingMinPercent=50;
  WrapTarget wrap;
  fui::ListItem balanced; balanced.label = "Một nhãn dài để dàn chữ"; balanced.value = "Giá trị dài";
  balanced.opensNext = false;
  const auto balancedLayout = fui::measureListRow(wrap, nullptr, 360, props, balanced);
  std::printf("balanced_calls=%d label_calls=%d lines=%d\n", wrap.measureCalls, wrap.labelCalls, balancedLayout.labelLines);
  if (wrap.labelCalls != 1 || balancedLayout.labelLines < 2) ++errors;
  props.action=1;
  fui::list(frame,{0,0,360,180},props);
  std::printf("measure_calls=%d\n", target.measureCalls);
  return errors || target.measureCalls >= 350 ? 1 : 0;
}
