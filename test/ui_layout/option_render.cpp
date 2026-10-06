#include "RecordingTarget.h"
#include "components/OptionPopupLayout.h"
int main() {
 int scenarios = 0;
 for (uint8_t tier : {0, 1, 2}) for (int width : {480, 528})
 for (const char* label : {"Cỡ chữ giao diện", "UI text size", "界面字号"}) for (int selected : {0, 15, 999}) {
  Target target(tier);
  fui::DeviceContext device; device.width = width; device.height = 792;
  fui::InteractionBuffer<17> interactions;
  fui::InputSnapshot input;
  fui::Frame<17> frame(target, device, input, interactions);
  const auto metrics = uiSizedThemeMetrics(TenorMetrics::values, tier);
  fui::DialogOption options[16];
  fui::OptionDialogProps props;
  props.title = label; props.titleText.font = 1; props.buttonText.font = 1;
  props.verticalOptions = true; props.options = options;
  props.padding = {20, 20, 20, 20}; props.gap = 8;
  props.buttonHeight = target.lineHeight(1) + 24;
  const int available = device.height - metrics.buttonHintsHeight - 12;
  const int popupWidth = width * 3 / 4;
  const int header = fui::optionDialogHeight(target, props, popupWidth);
  const auto window = optionPopupWindow(1000, selected, available, header, props.buttonHeight + props.gap);
  int slot = 0;
  if (window.paged) options[slot++] = {"<", 2, 0};
  for (int i = 0; i < window.count; ++i) options[slot++] = {label, 1, static_cast<int16_t>(window.first + i)};
  if (window.next) options[slot++] = {">", 2, 0};
  props.optionCount = slot;
  const int height = fui::optionDialogHeight(target, props, popupWidth);
  assert(height <= available);
  const fui::Rect rect{static_cast<int16_t>((width-popupWidth)/2), 9, static_cast<int16_t>(popupWidth), static_cast<int16_t>(height)};
  fui::optionDialog(frame, rect, props);
  bool selectedReachable = false;
  for (size_t i = 0; i < interactions.count(); ++i) {
    const auto& hit = interactions.data()[i];
    assert(hit.rect.y >= rect.y && hit.rect.bottom() <= rect.bottom());
    selectedReachable |= hit.action == 1 && hit.value == selected;
  }
  assert(selectedReachable);
  for (const auto& run : target.runs) {
    assert(run.x >= rect.x && run.right() <= rect.right());
    assert(run.y >= rect.y && run.bottom() <= rect.bottom());
  }
  ++scenarios;
 }
 printf("PASS: %d production option dialog renders keep selected hitbox and all text inside the page\n", scenarios);
}
