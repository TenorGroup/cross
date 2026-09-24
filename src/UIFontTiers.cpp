#include "UIFontTiers.h"
#include <GfxRenderer.h>
#include <FontCacheManager.h>
#include "SdCardFontSystem.h"
#include "components/UIScale.h"
#include <builtinFonts/geist_14_regular.h>
#include <builtinFonts/geist_14_bold.h>
#include <builtinFonts/geist_16_regular.h>
#include <builtinFonts/geist_16_bold.h>

// Existing small-tier families remain defined in main.cpp. Referencing them
// here avoids duplicating the five existing bitmap arrays in another unit.
extern EpdFontFamily smallFontFamily;
extern EpdFontFamily ui10FontFamily;
extern EpdFontFamily ui12FontFamily;

namespace {
EpdFont regular14(&geist_14_regular), bold14(&geist_14_bold);
EpdFont regular16(&geist_16_regular), bold16(&geist_16_bold);
EpdFontFamily family14(&regular14, &bold14);
EpdFontFamily family16(&regular16, &bold16);
}

const EpdFontFamily& uiFontTierFamily(UIFontRole role, uint8_t size) {
  const auto tier = normalizedUiTextSize(size);
  switch (role) {
    case UIFontRole::Caption:
      return tier == 0 ? smallFontFamily : tier == 1 ? ui10FontFamily : ui12FontFamily;
    case UIFontRole::Subtitle:
      return tier == 0 ? ui10FontFamily : tier == 1 ? ui12FontFamily : family14;
    case UIFontRole::Title:
      return tier == 0 ? family14 : family16;
    case UIFontRole::Body:
    default:
      return tier == 0 ? ui12FontFamily : tier == 1 ? family14 : family16;
  }
}

bool applyUiFontSize(GfxRenderer& renderer, uint8_t size) {
  static constexpr int ids[] = {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID};
  for (const int id : ids) {
    if (renderer.getFontMap().find(id) == renderer.getFontMap().end() || renderer.isSdCardFont(id)) return false;
  }
  if (auto* cache = renderer.getFontCacheManager()) cache->clearCache();
  renderer.replaceBuiltinFont(SMALL_FONT_ID, uiFontTierFamily(UIFontRole::Caption, size));
  renderer.replaceBuiltinFont(UI_10_FONT_ID, uiFontTierFamily(UIFontRole::Subtitle, size));
  renderer.replaceBuiltinFont(UI_12_FONT_ID, uiFontTierFamily(UIFontRole::Body, size));
  renderer.replaceBuiltinFont(UI_TITLE_FONT_ID, uiFontTierFamily(UIFontRole::Title, size));
  sdFontSystem.refreshUiFallbacks(renderer, normalizedUiTextSize(size));
  return true;
}
