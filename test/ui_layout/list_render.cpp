#include "RecordingTarget.h"
int main() {
  const ThemeMetrics themes[] = {BaseMetrics::values, LyraMetrics::values, TenorMetrics::values};
  const char* labels[] = {"Cỡ chữ giao diện", "UI text size", "界面字号"};
  int scenarios = 0;
  for (auto baseline : themes) for (uint8_t tier : {1, 2}) for (int width : {480, 528}) for (auto label : labels) {
    Target target(tier);
#ifdef LEGACY_METRICS
    const auto metrics = baseline;
#else
    const auto metrics = uiSizedThemeMetrics(baseline, tier);
#endif
    fui::DeviceContext device;
    device.width = width; device.height = 792;
    fui::InteractionBuffer<16> interactions;
    fui::InputSnapshot input;
    fui::Frame<16> frame(target, device, input, interactions);
    fui::ListItem items[6];
    for (auto& item : items) { item.label = label; item.subtitle = "Small / Medium / Large"; }
    fui::ListProps props;
    props.items = items; props.count = 6; props.rowHeight = metrics.listWithSubtitleRowHeight;
    props.rowGap = metrics.listRowGap; props.labelText.font = 1; props.subtitleText.font = 0;
    props.labelText.maxLines = 1; props.subtitleText.maxLines = 1;
    props.action = 1; props.selectedIndex = 0;
    const int stride = props.rowHeight + props.rowGap;
    fui::list(frame, {0, 0, static_cast<int16_t>(width), static_cast<int16_t>(stride * 3)}, props);
    assert(target.runs.size() == 6);
    for (size_t i = 0; i < target.runs.size(); i += 2) {
      const auto a = target.runs[i], b = target.runs[i + 1];
      const int top = static_cast<int>(i / 2) * stride;
      assert(a.y >= top);
      assert(a.bottom() <= b.y);
      assert(b.bottom() <= top + props.rowHeight);
      assert(a.x >= 0 && a.right() <= width);
      assert(b.x >= 0 && b.right() <= width);
    }
    ++scenarios;
  }
  printf("PASS: %d production SDK list render scenarios, label/subtitle bounds\n", scenarios);
}
