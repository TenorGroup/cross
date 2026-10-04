#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

class GfxRenderer;

// Main task mutates this under RenderLock; the common frame hook draws it under the same lock.
struct FrontlightGesture {
  bool vertical = true;
  uint8_t value = 0;
  uint32_t changedAt = 0;
  bool visible = false;
  bool dirty = false;

  static constexpr uint32_t IDLE_MS = 1500;
  bool expired(uint32_t now) const { return visible && now - changedAt >= IDLE_MS; }
  bool apply(uint8_t contacts, int dx, int dy, uint8_t brightness, uint8_t warmth, bool on, uint32_t now) {
    if (contacts != 2 || (dx == 0 && dy == 0)) return false;
    vertical = std::abs(dy) > std::abs(dx);
    const int pixels = vertical ? -dy : dx;
    const int delta = pixels / 6;
    if (!delta) return false;
    const int current = vertical ? (on ? brightness : 0) : warmth;
    value = static_cast<uint8_t>(std::clamp(current + delta, 0, 100));
    visible = true;
    changedAt = now;
    dirty = dirty || value != current;
    return true;
  }
  void draw(const GfxRenderer& renderer, int barTop) const;
};
