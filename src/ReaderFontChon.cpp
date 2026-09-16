#include "ReaderFontChon.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"

namespace fontdoc {

std::vector<Ho> danhSachHo(const SdCardFontRegistry* registry) {
  std::vector<Ho> ho;
  ho.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry ? registry->getFamilyCount() : 0));
  ho.push_back({I18N.get(StrId::STR_NOTO_SERIF), true, static_cast<uint8_t>(CrossPointSettings::NOTOSERIF)});
  ho.push_back({I18N.get(StrId::STR_NOTO_SANS), true, static_cast<uint8_t>(CrossPointSettings::NOTOSANS)});
  if (registry) {
    const auto& families = registry->getFamilies();
    for (size_t i = 0; i < families.size(); i++) {
      ho.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }
  return ho;
}

int hoDangDung(const SdCardFontRegistry* registry) {
  if (SETTINGS.sdFontFamilyName[0] != '\0' && registry) {
    const auto& families = registry->getFamilies();
    for (size_t i = 0; i < families.size(); i++) {
      if (families[i].name == SETTINGS.sdFontFamilyName) {
        return CrossPointSettings::BUILTIN_FONT_COUNT + static_cast<int>(i);
      }
    }
  }
  return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
}

bool apHo(GfxRenderer& renderer, const SdCardFontRegistry* registry, const int index) {
  if (index < 0) return false;
  if (index < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.fontFamily = static_cast<uint8_t>(index);
    SETTINGS.sdFontFamilyName[0] = '\0';
  } else {
    if (!registry) return false;
    const int sd = index - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry->getFamilies();
    if (sd >= static_cast<int>(families.size())) return false;
    strncpy(SETTINGS.sdFontFamilyName, families[sd].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  }
  sdFontSystem.ensureLoaded(renderer);  // go font the cu, nap font moi, hoac ve font nap san
  return true;
}

int coDangDung(const std::vector<uint8_t>& sizes) {
  const uint8_t pt = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
  for (size_t i = 0; i < sizes.size(); i++) {
    if (sizes[i] == pt) return static_cast<int>(i);
  }
  return 0;
}

void apCo(GfxRenderer& renderer, const uint8_t pt) {
  SETTINGS.fontPointSize = pt;
  sdFontSystem.ensureLoaded(renderer);
}

}  // namespace fontdoc
