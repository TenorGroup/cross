#include "UglyIconInk.h"

#include <algorithm>
#include <atomic>

#include "UglyInk.h"

namespace ugly {
namespace {
std::atomic<uint32_t> frameSeed{0};
}

void setIconFrameSeed(uint32_t seed) { frameSeed.store(seed, std::memory_order_relaxed); }
uint32_t iconFrameSeed() { return frameSeed.load(std::memory_order_relaxed); }

void icon(const GfxRenderer& renderer, icons::Id id, icons::Rect box, uint32_t seed, bool black) {
  seed ^= static_cast<uint32_t>(box.x) * 73856093u ^ static_cast<uint32_t>(box.y) * 19349663u;
  const int width = std::min(box.width, box.height) >= 22 ? 2 : 1;
  int offsetX = 0, offsetY = 0;
  if (id == icons::Id::Tick || id == icons::Id::Back || id == icons::Id::Left || id == icons::Id::Right ||
      id == icons::Id::Up || id == icons::Id::Down) {
    int minX = box.x + box.width, minY = box.y + box.height, maxX = box.x, maxY = box.y;
    icons::walk(id, box, seed, [&](icons::Point first, icons::Point second) {
      minX = std::min({minX, (first.x + 128) / 256, (second.x + 128) / 256});
      minY = std::min({minY, (first.y + 128) / 256, (second.y + 128) / 256});
      maxX = std::max({maxX, (first.x + 128) / 256, (second.x + 128) / 256});
      maxY = std::max({maxY, (first.y + 128) / 256, (second.y + 128) / 256});
    });
    const int tail = black ? width - 1 : 0;
    offsetX = (2 * (box.x + box.width / 2) - minX - maxX - tail) / 2;
    offsetY = (2 * (box.y + box.height / 2) - minY - maxY - tail) / 2;
  }
  icons::walk(id, box, seed, [&](icons::Point first, icons::Point second) {
    const int x0 = (first.x + 128) / 256 + offsetX, y0 = (first.y + 128) / 256 + offsetY;
    const int x1 = (second.x + 128) / 256 + offsetX, y1 = (second.y + 128) / 256 + offsetY;
    if (black) penStroke(renderer, x0, y0, x1, y1, width);
    else renderer.drawLine(x0, y0, x1, y1, false);
  });
}

// Fit the vector contour to 24 pixels of ink and keep the small icon's single-pixel pen.
void rowIcon(const GfxRenderer& renderer, icons::Id id, icons::Rect box, uint32_t seed) {
  seed ^= static_cast<uint32_t>(box.x) * 73856093u ^ static_cast<uint32_t>(box.y) * 19349663u;
  icons::Point low{25600, 25600}, high{0, 0};
  auto walk = [&](auto emit) { icons::walk(id, {0, 0, 100, 100}, seed, emit); };
  walk([&](auto first, auto second) {
    for (auto point : {first, second}) {
      low.x = std::min(low.x, point.x); low.y = std::min(low.y, point.y);
      high.x = std::max(high.x, point.x); high.y = std::max(high.y, point.y);
    }
  });
  const int span = std::max(high.x - low.x, high.y - low.y);
  if (span <= 0) return;
  const int width = ((high.x - low.x) * 23 + span / 2) / span;
  const int height = ((high.y - low.y) * 23 + span / 2) / span;
  const int left = box.x + (box.width - width - 1) / 2;
  const int top = box.y + (box.height - height - 1) / 2;
  auto pixel = [&](icons::Point point) {
    return icons::Point{left + ((point.x - low.x) * 23 + span / 2) / span,
                        top + ((point.y - low.y) * 23 + span / 2) / span};
  };
  walk([&](auto first, auto second) {
    first = pixel(first); second = pixel(second);
    penStroke(renderer, first.x, first.y, second.x, second.y, 1);
  });
}

}
