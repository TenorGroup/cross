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
  {
    SdCardFontManager manager;
    renderer.fonts.emplace(99, EpdFontFamily{});
    SdCardFont::builtinLayoutMatches = true;
    UI_CHECK(manager.loadBuiltinFamily(family, renderer, 18, 2, 99));
    UI_CHECK(manager.isBuiltinRaster() && renderer.sdFonts.count(99) == 1);
    manager.releaseReaderForDownload(renderer);
    UI_CHECK(!manager.isBuiltinRaster() && renderer.sdFonts.count(99) == 0);
    UI_CHECK(renderer.fonts.count(99) == 1 && SdCardFont::alive == 0);
    manager.unloadAll(renderer);
    renderer.fonts.clear();
    SdCardFont::builtinLayoutMatches = false;
  }
  {
    SdCardFontManager manager;
    UI_CHECK(manager.loadFamily(family, renderer, 18, 2));
    const int readerId = manager.getFontId(family.name);
    const int uiId = manager.loadFamilyExtraSize(family, renderer, 12);
    renderer.fallbacks[12] = uiId;
    manager.releaseReaderForDownload(renderer);
    UI_CHECK(SdCardFont::alive == 1 && renderer.sdFonts.count(readerId) == 0);
    UI_CHECK(manager.getFontId(family.name) == 0 && manager.currentPointSize() == 0);
    UI_CHECK(renderer.fallbacks.at(12) == uiId && renderer.sdFonts.count(uiId) == 1);
    manager.releaseReaderForDownload(renderer);
    UI_CHECK(SdCardFont::alive == 1);
    manager.unloadExtraSizes(renderer);
    UI_CHECK(SdCardFont::alive == 0 && renderer.fallbacks.empty());
    UI_CHECK(manager.loadFamily(family, renderer, 18, 2));
    UI_CHECK(manager.getFontId(family.name) != 0 && manager.currentPointSize() == 18);
    manager.unloadAll(renderer);
  }
  {
    SdCardFontManager manager;
    UI_CHECK(manager.loadFamily(family, renderer, 14));
    const int sharedId = manager.getFontId(family.name);
    renderer.fallbacks[14] = sharedId;
    manager.releaseReaderForDownload(renderer);
    UI_CHECK(SdCardFont::alive == 1 && manager.getFontId(family.name) == sharedId);
    UI_CHECK(renderer.fallbacks.at(14) == sharedId && renderer.sdFonts.count(sharedId) == 1);
    manager.unloadAll(renderer);
  }
  UI_CHECK(SdCardFont::alive == 0);
  std::puts("PASS: 450 tier changes preserve reader object/ID/weight, at most four live fonts");
}
