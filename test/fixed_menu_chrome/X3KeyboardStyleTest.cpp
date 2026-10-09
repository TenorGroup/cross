#include "test/ui_layout/RecordingTarget.h"

namespace tenorchrome { bool x3ScreenLayoutEnabled() { return true; } }

PRODUCTION_URL_LAYERS
enum class InputType { Text, Password, Url };
struct KeyboardLayoutBoundary {
  bool symbols, shifted, urlPanel;
  InputType inputType;
  fui::KeyboardLayoutId layoutId = fui::KeyboardLayoutId::QwertyEn;
  bool showLangKey = false;
  PRODUCTION_LAYOUT_METHOD
};
struct KeyboardCase { int x, y, width, height, tier, bodyLine, captionLine, locale, type, rowHeight, symbols, shifted, urlPanel, cursor; };
const KeyboardCase cases[] = {
  // KEYBOARD_CASES
};
struct KeyboardTarget : Target {
  int bodyLine, captionLine;
  int outlines = 0;
  fui::Rect body;
  KeyboardTarget(int tier, int primary, int caption, fui::Rect rect)
      : Target(tier), bodyLine(primary), captionLine(caption), body(rect) {}
  int16_t lineHeight(fui::FontId font) const override { return font == 0 ? captionLine : bodyLine; }
  void stroke(fui::Rect rect, fui::Paint, uint8_t width, uint8_t radius, uint8_t) override {
    assert(rect.y >= body.y && rect.bottom() <= body.bottom());
    assert(width == 3 && radius == 8);
    ++outlines;
  }
};
int main() {
  int count = 0;
  const int compactBody = cases[0].bodyLine, compactCaption = cases[0].captionLine;
  for (const auto& sample : cases) {
    for (const bool aligned : {true, false}) {
    KeyboardLayoutBoundary page{sample.symbols != 0, sample.shifted != 0, sample.urlPanel != 0, static_cast<InputType>(sample.type)};
    const auto& layout = page.currentLayout();
    const int rowHeight = (sample.height - (layout.rowCount - 1) * fui::KeyboardProps{}.rowGap) / layout.rowCount;
    const bool compact = sample.tier != 0 && rowHeight < sample.bodyLine + sample.captionLine + 8;
    const fui::Rect body{static_cast<int16_t>(sample.x), static_cast<int16_t>(sample.y),
                        static_cast<int16_t>(sample.width), static_cast<int16_t>(sample.height)};
    KeyboardTarget target(sample.tier, compact ? compactBody : sample.bodyLine,
                          compact ? compactCaption : sample.captionLine, body);
    fui::DeviceContext device;
    device.width = sample.width;
    device.height = 792;
    fui::InteractionBuffer<56> interactions;
    fui::InputSnapshot input;
    fui::Frame<56> frame(target, device, input, interactions);
    fui::KeyboardProps props;
    props.layout = &layout;
    props.alignedColumns = aligned;
    props.keyAction = 1;
    props.selectedIndex = sample.cursor ? -1 : 0;
    props.labelText.font = 1;
    props.altText.font = 0;
    props.stackAlternates = sample.tier != 0;
    props.padding = {0, 0, 0, 0};
    props.gap = 3;
    props.okLabel = "OK";
    props.shiftLabel = "aA";
    props.modeLabel = "?123";
    // KEYBOARD_STYLE
    fui::keyboard(frame, body, props);
    assert(target.outlines == (sample.cursor ? 0 : 1));
    assert(!target.runs.empty());
    for (size_t index = 0; index < target.runs.size(); ++index) {
      const auto& current = target.runs[index];
      assert(current.x >= body.x && current.right() <= body.right());
      assert(current.y >= body.y && current.bottom() <= body.bottom());
      for (size_t previous = 0; previous < index; ++previous) {
        const auto& other = target.runs[previous];
        assert(!(current.x < other.right() && other.x < current.right() &&
                 current.y < other.bottom() && other.y < current.bottom()));
      }
    }
    ++count;
    }
  }
  std::printf("GREEN: %d SDK keyboard cases, 3 px selected outline, no overlapping labels\n", count);
}
