#include "RecordingTarget.h"
struct KeyboardTarget : Target {
  using Target::Target;
  int16_t lineHeight(fui::FontId font) const override { return font == 0 ? spec.captionLineHeight : spec.bodyLineHeight; }
};
int main() {
 for (uint8_t tier : {1, 2}) for (int width : {480, 528}) {
  KeyboardTarget target(tier);
  fui::DeviceContext device; device.width = width; device.height = 792;
  fui::InteractionBuffer<56> interactions;
  fui::InputSnapshot input;
  fui::Frame<56> frame(target, device, input, interactions);
  fui::KeyboardProps props;
  const auto& layout = fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, false, false, true);
  props.layout = &layout; props.keyAction = 1; props.labelText.font = 1; props.altText.font = 0;
  props.padding = {0, 0, 0, 0}; props.gap = 3;
#ifndef LEGACY_METRICS
  props.stackAlternates = true;
#endif
  const int rowHeight = target.spec.bodyLineHeight + target.spec.captionLineHeight + 8;
  const int height = layout.rowCount * rowHeight + (layout.rowCount - 1) * 3;
  fui::keyboard(frame, {8, 170, static_cast<int16_t>(width - 16), static_cast<int16_t>(height)}, props);
  assert(target.runs.size() >= 40);
  for (size_t i = 0; i < target.runs.size(); ++i) {
    const auto a = target.runs[i];
    assert(a.x >= 8 && a.right() <= width - 8);
    assert(a.y >= 170 && a.bottom() <= 170 + height);
    for (size_t j = 0; j < i; ++j) {
      const auto b = target.runs[j];
      const bool overlap = a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
      assert(!overlap);
    }
  }
 }
 puts("PASS: production SDK five-row keyboard main/alternate labels do not overlap at both larger tiers and widths");
}
