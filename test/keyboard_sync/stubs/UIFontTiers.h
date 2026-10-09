#pragma once
#include <GfxRenderer.h>

enum class UIFontRole { Caption, Subtitle };
inline const EpdFontFamily& uiFontTierFamily(UIFontRole, uint8_t) {
  static EpdFontFamily family;
  return family;
}
