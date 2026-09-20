#include "AlwaysCheck.h"
#include "components/UIScale.h"
int main() {
  const auto small = uiTextSizeSpec(0);
  const auto medium = uiTextSizeSpec(1);
  const auto large = uiTextSizeSpec(2);
  UI_CHECK(small.captionPointSize == 8 && small.subtitlePointSize == 10 && small.bodyPointSize == 12);
  UI_CHECK(medium.captionPointSize == 10 && medium.subtitlePointSize == 12 && medium.bodyPointSize == 14);
  UI_CHECK(large.captionPointSize == 12 && large.subtitlePointSize == 14 && large.bodyPointSize == 16);
  UI_CHECK(small.captionLineHeight == 21 && small.subtitleLineHeight == 26 && small.bodyLineHeight == 33);
  UI_CHECK(medium.bodyLineHeight > small.bodyLineHeight && large.bodyLineHeight > medium.bodyLineHeight);
  UI_CHECK(uiTextSizeSpec(255).bodyPointSize == small.bodyPointSize);
  UI_CHECK(uiScaleSpec().smallFontId == UI_10_FONT_ID);
  UI_CHECK(uiScaleSpec().bodyFontId == UI_12_FONT_ID);
}
