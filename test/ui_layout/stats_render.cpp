#include <GfxRenderer.h>
#include <I18n.h>
#include <cassert>
#include <cstdio>
#include "components/ReadingStatsView.h"
int main() {
 int scenarios = 0;
 for (uint8_t tier : {1, 2}) for (int width : {480, 528}) for (int locale : {0, 1, 2}) for (int page : {0, 1, 2}) {
  SETTINGS.uiTextSize = tier; statsLocale = locale;
  GfxRenderer renderer; renderer.width = width;
  if (page == 2) readingstatsview::drawSleep(renderer);
  else readingstatsview::draw(renderer, 162, false, false, page);
  for (size_t i = 0; i < renderer.runs.size(); ++i) {
    const auto& a = renderer.runs[i];
    if (a.x < 0 || a.x + a.width > width || a.y < 0 || a.y + a.height > renderer.height) {
      printf("bounds tier=%u locale=%d page=%d text=%s x=%d w=%d y=%d h=%d\n",tier,locale,page,a.text.c_str(),a.x,a.width,a.y,a.height);
      return 1;
    }
    for (size_t j = 0; j < i; ++j) {
      const auto& b = renderer.runs[j];
      if (a.x < b.x+b.width && b.x < a.x+a.width && a.y < b.y+b.height && b.y < a.y+a.height) {
        printf("overlap tier=%u locale=%d page=%d %s / %s\n",tier,locale,page,a.text.c_str(),b.text.c_str());
        return 1;
      }
    }
  }
  ++scenarios;
 }
 printf("PASS: %d production stats page/sleep renders, three real locales, full line-box bounds\n",scenarios);
}
