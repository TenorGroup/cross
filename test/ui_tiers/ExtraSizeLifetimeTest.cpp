#include "AlwaysCheck.h"
#include <cstdio>
#include <SdCardFontManager.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>
#include <GfxRenderer.h>
int main() {
  GfxRenderer renderer;
  SdCardFontFamilyInfo family;
  for (const auto config : {std::pair<int,int>{18,2}, {14,0}, {14,2}}) {
    SdCardFontManager manager;
    UI_CHECK(manager.loadFamily(family, renderer, config.first, config.second));
    const int readerId = manager.getFontId(family.name);
    const auto* reader = renderer.sdFonts.at(readerId);
    for (int cycle = 0; cycle < 50; ++cycle) {
      for (const int first : {8,10,12}) {
        manager.unloadExtraSizes(renderer);
        UI_CHECK(SdCardFont::alive == 1 && renderer.fonts.size() == 1);
        UI_CHECK(renderer.fallbacks.empty());
        for (int size = first; size <= first+4; size += 2) {
          const int id = manager.loadFamilyExtraSize(family, renderer, size);
          UI_CHECK(id);
          renderer.fallbacks[size] = id;
        }
        UI_CHECK(SdCardFont::alive <= 4 && renderer.fonts.size() <= 4);
        UI_CHECK(manager.getFontId(family.name) == readerId);
        UI_CHECK(renderer.sdFonts.at(readerId) == reader);
        UI_CHECK(manager.currentPointSize() == config.first && manager.currentWeight() == config.second);
      }
    }
    manager.unloadAll(renderer);
    UI_CHECK(renderer.fonts.empty() && renderer.sdFonts.empty() && renderer.fallbacks.empty());
  }
  UI_CHECK(SdCardFont::alive == 0);
  std::puts("PASS: 450 tier changes preserve reader object/ID/weight, at most four live fonts");
}
