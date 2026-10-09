#pragma once

#include <GfxRenderer.h>

#include "UglyIcons.h"

namespace ugly {

void setIconFrameSeed(uint32_t seed);
uint32_t iconFrameSeed();
void icon(const GfxRenderer& renderer, icons::Id id, icons::Rect box, uint32_t seed, bool black = true);
inline icons::Rect rowInkBox(int left, int textTop, int textBottom) {
  return {left, textTop, 24, textBottom - textTop};
}
void rowIcon(const GfxRenderer& renderer, icons::Id id, icons::Rect box, uint32_t seed);
bool iconAsset(const GfxRenderer& renderer, const uint8_t* bits, icons::Rect box, uint32_t seed,
               int nativeWidth = 0, int nativeHeight = 0, bool rowInk = false);
bool iconSymbol(const GfxRenderer& renderer, int shape, int x, int y, int size, bool black);

}
