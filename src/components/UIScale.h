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

// Line heights are measured from the generated font data at 150 DPI.
constexpr UITextSizeSpec uiTextSizeSpec(uint8_t value) {
  switch (normalizedUiTextSize(value)) {
    case 1: return {10, 12, 14, 26, 33, 38};
    case 2: return {12, 14, 16, 33, 38, 43};
    default: return {8, 10, 12, 21, 26, 33};
  }
}

constexpr UIScaleSpec uiScaleSpec() { return {UI_10_FONT_ID, UI_12_FONT_ID, UI_12_FONT_ID}; }
