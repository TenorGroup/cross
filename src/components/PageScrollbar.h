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
  // X4 Pro: x 469-474, with 5 clear pixels between the thumb and the frame.
  static constexpr int PAGE_SCROLLBAR_EDGE_INSET = 5;

  static PageScrollbar& instance() {
    static PageScrollbar scrollbar;
    return scrollbar;
  }

  void activate(const void* activity) {
    if (!tenorchrome::kTouchShell || activityOwner == activity) return;
    activityOwner = activity;
    regionOwner = nullptr;
    available = false;
    idle.hide();
    cancelIdleRequest();
  }

  void beginPaint() { painted = false; }

  void endPaint(const GfxRenderer* renderer = nullptr) {
    if (painted || !tenorchrome::kTouchShell) return;
    if (renderer && available.load())
      renderer->fillRect(region.left, region.top, tenorchrome::FRAME_BAR_WIDTH, region.bottom - region.top, false);
    available = false;
    regionOwner = nullptr;
    idle.hide();
    cancelIdleRequest();
  }

  void draw(const GfxRenderer& renderer, freeink::ui::DrawTarget& target, const int top, const int bottom,
            const int length, const int offset, const void* owner) {
    if (!tenorchrome::kTouchShell) return;
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
    const int left = renderer.getScreenWidth() - PAGE_SCROLLBAR_EDGE_INSET - tenorchrome::FRAME_BAR_WIDTH;
    if (available.load() && (regionOwner != owner || region.left != left || region.top != top || region.bottom != bottom))
      renderer.fillRect(region.left, region.top, tenorchrome::FRAME_BAR_WIDTH, region.bottom - region.top, false);
    if (regionOwner != owner || region.left != left || region.top != top || region.bottom != bottom ||
        region.length != length || region.offset != clampedOffset)
      idle.show(millis());
    region = {left, top, bottom, length, clampedOffset};
    regionOwner = owner;
    available = true;
    cancelIdleRequest();
    renderer.fillRect(left, top, tenorchrome::FRAME_BAR_WIDTH, height, false);
    if (idle.isVisible()) paint(target);
  }

  void drawList(const GfxRenderer& renderer, freeink::ui::DrawTarget& target, const freeink::ui::Rect viewport,
                const freeink::ui::ListNav& nav, const int count) {
    const int rows = std::max(1, nav.pageRowsFor(count));
    draw(renderer, target, viewport.y, viewport.bottom(), count * viewport.height / rows,
          nav.top * viewport.height / rows, &nav);
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
    if (!tenorchrome::kTouchShell || !available.load()) return;
    renderer.fillRect(region.left, region.top, tenorchrome::FRAME_BAR_WIDTH, region.bottom - region.top, false);
    if (idle.isVisible()) paint(target);
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
