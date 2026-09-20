#pragma once

// Each layout pass moves the viewport toward its selected row. All custom
// list surfaces settle that feedback before publishing the framebuffer.
template <typename Navigation, typename Paint>
int renderSettledList(Navigation& navigation, Paint&& paint) {
  int passes = 0;
  do {
    paint();
    ++passes;
  } while (navigation.consumeRebuildNeeded() && passes < 9);
  return passes;
}
