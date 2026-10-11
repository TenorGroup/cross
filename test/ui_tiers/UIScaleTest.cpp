#include "AlwaysCheck.h"
#include "components/UIScale.h"
#include <builtinFonts/geist_8_regular.h>
#include <builtinFonts/geist_10_regular.h>
#include <builtinFonts/geist_12_regular.h>
#include <builtinFonts/geist_14_regular.h>
#include <builtinFonts/geist_16_regular.h>

// The line height the firmware lays out with: advanceY of the generated face at that point size.
static int lineHeightOfFace(uint8_t pointSize) {
  switch (pointSize) {
    case 8: return geist_8_regular.advanceY;
    case 10: return geist_10_regular.advanceY;
    case 12: return geist_12_regular.advanceY;
    case 14: return geist_14_regular.advanceY;
    case 16: return geist_16_regular.advanceY;
  }
  UI_CHECK(false);
  return 0;
}

int main() {
  for (uint8_t tier = 0; tier < 3; ++tier) {
    const auto spec = uiTextSizeSpec(tier);
    UI_CHECK(spec.captionLineHeight == lineHeightOfFace(spec.captionPointSize));
    UI_CHECK(spec.subtitleLineHeight == lineHeightOfFace(spec.subtitlePointSize));
    UI_CHECK(spec.bodyLineHeight == lineHeightOfFace(spec.bodyPointSize));
    // A tier's layout step never reserves less than its face grows over the Small tier.
    const auto small = uiTextSizeSpec(0);
    const auto growth = uiLayoutGrowth(tier);
    UI_CHECK(growth.caption >= spec.captionLineHeight - small.captionLineHeight);
    UI_CHECK(growth.subtitle >= spec.subtitleLineHeight - small.subtitleLineHeight);
    UI_CHECK(growth.body >= spec.bodyLineHeight - small.bodyLineHeight);
  }
  UI_CHECK(uiLayoutGrowth(0).caption == 0 && uiLayoutGrowth(0).subtitle == 0 && uiLayoutGrowth(0).body == 0);
  UI_CHECK(uiLayoutGrowth(255).body == 0);
  const auto small = uiTextSizeSpec(0);
  const auto medium = uiTextSizeSpec(1);
  const auto large = uiTextSizeSpec(2);
  UI_CHECK(small.captionPointSize == 8 && small.subtitlePointSize == 10 && small.bodyPointSize == 12);
  UI_CHECK(medium.captionPointSize == 10 && medium.subtitlePointSize == 12 && medium.bodyPointSize == 14);
  UI_CHECK(large.captionPointSize == 12 && large.subtitlePointSize == 14 && large.bodyPointSize == 16);
  UI_CHECK(medium.bodyLineHeight > small.bodyLineHeight && large.bodyLineHeight > medium.bodyLineHeight);
  UI_CHECK(uiTextSizeSpec(255).bodyPointSize == small.bodyPointSize);
  UI_CHECK(uiScaleSpec().smallFontId == UI_10_FONT_ID);
  UI_CHECK(uiScaleSpec().bodyFontId == UI_12_FONT_ID);
}
