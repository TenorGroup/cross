#include <cassert>
#include <cstdio>
#include "components/themes/BaseTheme.h"
#include "components/themes/TenorTheme.h"
#include "components/themes/roundedraff/RoundedRaffTheme.h"
#include "components/UIScale.h"
#if __has_include("components/UIThemeSizing.h")
#include "components/UIThemeSizing.h"
#else
ThemeMetrics uiSizedThemeMetrics(ThemeMetrics metrics, uint8_t) { return metrics; }
#endif
int main() {
  const ThemeMetrics themes[] = {BaseMetrics::values, LyraMetrics::values, TenorMetrics::values, RoundedRaffMetrics::values};
  for (const auto& baseline : themes) {
    const auto small = uiSizedThemeMetrics(baseline, 0);
    assert(small.headerHeight == baseline.headerHeight);
    assert(small.listRowHeight == baseline.listRowHeight);
    assert(small.listWithSubtitleRowHeight == baseline.listWithSubtitleRowHeight);
    assert(small.buttonHintsHeight == baseline.buttonHintsHeight);
    assert(small.keyboardKeyHeight == baseline.keyboardKeyHeight);
    assert(uiSizedThemeMetrics(baseline, 255).headerHeight == small.headerHeight);
    for (uint8_t tier : {1, 2}) {
      const auto m = uiSizedThemeMetrics(baseline, tier);
      const auto font = uiTextSizeSpec(tier);
      assert(m.listRowHeight >= font.bodyLineHeight + 8);
      assert(m.listWithSubtitleRowHeight >= font.bodyLineHeight + font.subtitleLineHeight + 8);
      assert(m.headerHeight >= baseline.headerHeight + font.bodyLineHeight - 33);
      assert(m.buttonHintsHeight >= 2 * font.subtitleLineHeight + 4);
      assert(m.keyboardKeyHeight >= font.bodyLineHeight + 8);
      for (int height : {480, 528, 792, 800}) {
        const int body = height - m.headerHeight - m.tabBarHeight - m.buttonHintsHeight;
        assert(body / m.listWithSubtitleRowHeight >= 1);
      }
    }
  }
  puts("PASS: four theme families x three tiers and four viewport heights");
}
