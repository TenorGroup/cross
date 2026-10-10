#pragma once

#include <FreeInkApp.h>

#include <algorithm>
#include <atomic>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/PageScrollbarIdle.h"
#include "components/TenorMenuChrome.h"
#include "components/UiAppHelpers.h"

class PageScrollbar {
 public:
  // X3: x 517-522; X4 Pro: x 469-474. The thumb clears the frame by 5 pixels.
  static constexpr int PAGE_SCROLLBAR_EDGE_INSET = 5;

  static PageScrollbar& instance() {
    static PageScrollbar scrollbar;
    return scrollbar;
  }

  void activate(const void* activity) {
    if (activityOwner == activity) return;
    activityOwner = activity;
    regionOwner = nullptr;
    available = false;
    idle.hide();
    cancelIdleRequest();
  }

  void beginPaint() { painted = false; }

  void endPaint(const GfxRenderer* renderer = nullptr) {
    if (painted) return;
    if (renderer && available.load())
      renderer->fillRect(region.left, region.top, tenorchrome::FRAME_BAR_WIDTH, region.bottom - region.top, false);
    available = false;
    regionOwner = nullptr;
    idle.hide();
    cancelIdleRequest();
  }

  void draw(const GfxRenderer& renderer, freeink::ui::DrawTarget& target, const int top, const int bottom,
            const int length, const int offset, const void* owner, const int frameRight = 0) {
    painted = true;
    const int height = bottom - top;
    if (height <= 0 || length <= height) {
      if (available.load())
        renderer.fillRect(region.left, region.top, tenorchrome::FRAME_BAR_WIDTH, region.bottom - region.top, false);
      available = false;
      regionOwner = nullptr;
      idle.hide();
      cancelIdleRequest();
      return;
    }
    const int clampedOffset = std::clamp(offset, 0, length - height);
    const int edgeLeft = renderer.getScreenWidth() - PAGE_SCROLLBAR_EDGE_INSET - tenorchrome::FRAME_BAR_WIDTH;
    const int left = !tenorchrome::kTouchShell && frameRight > 0
                         ? std::min(edgeLeft, frameRight + PAGE_SCROLLBAR_EDGE_INSET) : edgeLeft;
    if (available.load() && (regionOwner != owner || region.left != left || region.top != top || region.bottom != bottom))
      renderer.fillRect(region.left, region.top, tenorchrome::FRAME_BAR_WIDTH, region.bottom - region.top, false);
    if (tenorchrome::kTouchShell &&
        (regionOwner != owner || region.left != left || region.top != top || region.bottom != bottom ||
         region.length != length || region.offset != clampedOffset))
      idle.show(millis());
    region = {left, top, bottom, length, clampedOffset};
    regionOwner = owner;
    available = true;
    cancelIdleRequest();
    renderer.fillRect(left, top, tenorchrome::FRAME_BAR_WIDTH, height, false);
    if (!tenorchrome::kTouchShell || idle.isVisible()) paint(target);
  }

  void drawList(const GfxRenderer& renderer, freeink::ui::DrawTarget& target, const freeink::ui::Rect viewport,
                const freeink::ui::ListNav& nav, const int count) {
    const int rows = std::max(1, nav.pageRowsFor(count));
    draw(renderer, target, viewport.y, viewport.bottom(), count * viewport.height / rows,
          nav.top * viewport.height / rows, &nav);
  }

  // Whether the painted thumb stands over row `y` (an arrow of the edge drawn there must turn white to be read).
  bool thumbCovers(const int y) const {
    if (!available.load()) return false;
    const int height = region.bottom - region.top;
    if (height <= 0 || region.length <= height || (tenorchrome::kTouchShell && !idle.isVisible())) return false;
    const int start = region.top + static_cast<int>(static_cast<long>(region.offset) * height / region.length);
    const int size = std::max(1, static_cast<int>(static_cast<long>(height) * height / region.length));
    return y >= start && y < start + size;
  }

  void noteInput(const MappedInputManager& input) {
    if (!tenorchrome::kTouchShell || !available.load()) return;
    int deltaY = 0;
    unsigned long heldMs = 0;
    if (!input.wasVerticalSwipe(deltaY, heldMs)) return;
    const bool wasVisible = idle.isVisible();
    idle.show(millis());
    showPending = !wasVisible;
    hidePending = false;
  }

  bool needsIdleUpdate(const MappedInputManager& input) {
    if (!tenorchrome::kTouchShell || !available.load()) return false;
    int touchX = 0, touchY = 0;
    if (input.isScreenTouchHeld(touchX, touchY)) return false;
    if (showPending.load()) return true;
    if (!idle.expired(millis())) return false;
    hidePending = true;
    return true;
  }

  void cancelIdleRequest() {
    hidePending = false;
    showPending = false;
  }

  bool renderIdleUpdate(const GfxRenderer& renderer, const MappedInputManager& input) {
    if (!tenorchrome::kTouchShell) return false;
    const bool hide = hidePending.exchange(false);
    const bool show = showPending.exchange(false);
    if (!hide && !show) return false;
    if (!available.load()) return false;
    int touchX = 0, touchY = 0;
    if (input.isScreenTouchHeld(touchX, touchY)) return true;
    if (hide && !show && !idle.expired(millis())) return true;
    renderer.waitRefreshComplete();
    const uint32_t generation = idle.generation();
    renderer.fillRect(region.left, region.top, tenorchrome::FRAME_BAR_WIDTH, region.bottom - region.top, false);
    if (show) {
      auto target = makeUiTarget(renderer);
      paint(target);
    }
    renderer.displayWindow(region.left, region.top, tenorchrome::FRAME_BAR_WIDTH, region.bottom - region.top);
    if (!show) {
      idle.hideIfUnchanged(generation);
      if (idle.generation() != generation) showPending = true;
    }
    return true;
  }

  void repaint(const GfxRenderer& renderer, freeink::ui::DrawTarget& target) const {
    if (!available.load()) return;
    renderer.fillRect(region.left, region.top, tenorchrome::FRAME_BAR_WIDTH, region.bottom - region.top, false);
    if (!tenorchrome::kTouchShell || idle.isVisible()) paint(target);
  }

 private:
  void paint(freeink::ui::DrawTarget& target) const {
    const int height = region.bottom - region.top;
    freeink::ui::drawListScrollIndicator(target,
        {static_cast<int16_t>(region.left), static_cast<int16_t>(region.top), tenorchrome::FRAME_BAR_WIDTH,
         static_cast<int16_t>(height)}, region.length, height, region.offset, tenorchrome::FRAME_BAR_WIDTH);
  }

  struct Region {
    int left = 0, top = 0, bottom = 0, length = 0, offset = 0;
  } region;
  PageScrollbarIdle idle;
  std::atomic<bool> available{false};
  std::atomic<bool> hidePending{false};
  std::atomic<bool> showPending{false};
  const void* activityOwner = nullptr;
  const void* regionOwner = nullptr;
  bool painted = false;
};
