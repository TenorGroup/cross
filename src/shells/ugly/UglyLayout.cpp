#include "UglyLayout.h"

#include <string>
#include <vector>

#include "UglyChrome.h"

namespace fui = freeink::ui;

namespace ugly {
namespace {
struct Run {
  fui::Rect rect;
  std::string text;
  fui::TextAlign align;
  bool locked;
  uint8_t lines;
};
}  // namespace

void layoutByHand(const GfxRenderer& renderer, fui::GfxRendererTarget& target, const std::function<void()>& layout) {
  std::vector<Run> runs;
  target.setPaintingEnabled(false);
  target.setTextSink(
      [](void* ctx, const fui::Rect rect, const char* text, const fui::TextStyle& style) {
        // Words in light gray belong to a row that cannot be chosen now.
        static_cast<std::vector<Run>*>(ctx)->push_back(
            {rect, text, style.align, !style.inverted && style.color == fui::Color::LightGray, style.maxLines});
      },
      &runs);
  layout();
  target.setTextSink(nullptr, nullptr);
  target.setPaintingEnabled(true);
  for (const auto& run : runs) uglychrome::words(renderer, run.rect, run.text.c_str(), run.align, run.locked, run.lines);
}

}  // namespace ugly
