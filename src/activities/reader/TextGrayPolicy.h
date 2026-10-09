#pragma once

#include <cstddef>

struct TextGrayContext {
  bool textAntiAliasing = false;
  bool covered = false;
  bool preparingPopup = false;
  bool pendingManualTurn = false;
  bool pendingExternalTurn = false;
  bool pageBeingLeft = false;
  bool nextScreenWaiting = false;
  bool pendingScreenshot = false;
  bool backgroundWork = false;
  bool preview = false;
};

inline bool shouldRunTextGrayscale(const TextGrayContext& context) {
  return context.textAntiAliasing && !context.covered && !context.preparingPopup && !context.pendingManualTurn &&
         !context.pendingExternalTurn && !context.pageBeingLeft && !context.nextScreenWaiting &&
         !context.pendingScreenshot && !context.backgroundWork;
}

inline bool hasGrayscaleHeapFloor(const size_t freeHeap, const size_t largestBlock) {
  return freeHeap >= 10240 && largestBlock >= 8192;
}
