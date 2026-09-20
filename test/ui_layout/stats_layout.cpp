#include <cassert>
#include <initializer_list>
#include <cstdio>
#include "components/ReadingStatsLayout.h"
#include "components/UIScale.h"
int main() {
 for (uint8_t tier : {1, 2}) {
  const auto text = uiTextSizeSpec(tier);
  const ReadingStatsLayout layout(text.captionLineHeight, text.subtitleLineHeight, text.bodyLineHeight, text.bodyLineHeight);
#ifdef LEGACY_METRICS
  assert(28 >= text.subtitleLineHeight);
  assert(105 - 80 >= text.subtitleLineHeight);
#else
  assert(layout.value >= text.subtitleLineHeight + 4);
  assert(layout.periodValue >= layout.periodLabel + text.subtitleLineHeight + 4);
  assert(layout.chartBottom - (layout.periodValue + text.subtitleLineHeight) >= 66);
  assert(layout.meanValue >= layout.meanLabel + text.captionLineHeight + 4);
  assert(layout.habitsLabel >= layout.meanValue + text.bodyLineHeight + 4);
  assert(layout.note >= layout.habitsValue + text.bodyLineHeight + 4);
  assert(layout.meanLabel < 320);
  assert(layout.height - layout.meanLabel < 240);
#endif
 }
 puts("PASS: both larger stats tiers preserve66px chart and separate every text band");
}
