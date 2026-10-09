#include "UglyIconInk.h"

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
  icons::walk(id, box, seed, [&](icons::Point first, icons::Point second) {
    const int x0 = (first.x + 128) / 256, y0 = (first.y + 128) / 256;
    const int x1 = (second.x + 128) / 256, y1 = (second.y + 128) / 256;
    if (black) penStroke(renderer, x0, y0, x1, y1, width);
    else renderer.drawLine(x0, y0, x1, y1, false);
  });
}

}
