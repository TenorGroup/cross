#include "RecordingTarget.h"
#include "components/SettledListRender.h"

int main() {
  int scenarios = 0;
  for (uint8_t tier : {0, 1, 2}) for (int offset : {0, 1}) for (int selected : {2, 8, 13}) {
    Target target(tier);
    fui::DeviceContext device;
    device.width = 480; device.height = 792;
    fui::InteractionBuffer<16> interactions;
    fui::InputSnapshot input;
    fui::ListItem items[14];
    for (auto& item : items) item.label = "A long setting label which needs two wrapped lines";
    fui::ListProps props;
    props.items = items; props.count = 14; props.action = 1;
    props.rowHeight = 52; props.rowGap = 4; props.labelText.font = 1;
    props.labelText.maxLines = 2;
    fui::ListNav nav;
    nav.selected = selected + offset;
    nav.visibleRows = 2;
    nav.top = selected - 1;
    nav.followPending = true;
    props.nav = &nav;
    bool visible = false;
    const int passes = renderSettledList(nav, [&] {
      interactions.clear();
      fui::Frame<16> frame(target, device, input, interactions);
      props.selectedIndex = selected;
      props.topIndex = nav.top;
      fui::list(frame, {0, 0, 240, 112}, props);
      assert(nav.selected == selected + offset);
      visible = nav.top <= selected && nav.top + nav.drawnRows > selected;
    });
    assert(passes <= 9);
    assert(visible);
    ++scenarios;
  }
  printf("PASS: %d actual SDK wrapped-list follow scenarios, ordinary and ring cursors\n", scenarios);
}
