#pragma once
#include <cstdint>
#include "fontIds.h"

// Stable aliases keep drawing and measurement on the same active UI faces.
// Reader font IDs are independent of these three bindings.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

struct UITextSizeSpec {
  uint8_t captionPointSize;
  uint8_t subtitlePointSize;
  uint8_t bodyPointSize;
  uint8_t captionLineHeight;
  uint8_t subtitleLineHeight;
  uint8_t bodyLineHeight;
};

constexpr uint8_t normalizedUiTextSize(uint8_t value) { return value < 3 ? value : 0; }

// Line heights are the advanceY of the generated Geist face at that point size; UIScaleTest reads them from the font data.
constexpr UITextSizeSpec uiTextSizeSpec(uint8_t value) {
  switch (normalizedUiTextSize(value)) {
    case 1: return {10, 12, 14, 27, 33, 38};
    case 2: return {12, 14, 16, 33, 38, 43};
    default: return {8, 10, 12, 22, 27, 33};
  }
}

// How far each tier's layout grows from the Small tier, in px. Screens were laid out with these steps, one px
// above the real growth of the Geist faces for caption and subtitle at the larger tiers; the measured heights
// above must not move a screen, so the steps stay their own numbers. A step never reserves less than the font grows.
struct UILayoutGrowth {
  uint8_t caption;
  uint8_t subtitle;
  uint8_t body;
};
constexpr UILayoutGrowth uiLayoutGrowth(uint8_t value) {
  switch (normalizedUiTextSize(value)) {
    case 1: return {5, 7, 5};
    case 2: return {12, 12, 10};
    default: return {0, 0, 0};
  }
}

constexpr UIScaleSpec uiScaleSpec() { return {UI_10_FONT_ID, UI_12_FONT_ID, UI_12_FONT_ID}; }
