// The registry's catalog walk, replaced by a list the test controls so each test can count how
// many times a boot walked the card. File naming follows the real registry.
#include <SdCardFontRegistry.h>

#include <algorithm>

#include "HostCard.h"

std::vector<SdCardFontFamilyInfo> hostCatalog;
int hostDiscoveries = 0;

std::string SdCardFontFamilyInfo::dir() const {
  return std::string(hiddenRoot ? SdCardFontRegistry::FONTS_DIR_HIDDEN : SdCardFontRegistry::FONTS_DIR_VISIBLE) + "/" +
         name;
}

std::string SdCardFontFamilyInfo::filePath(const SdCardFontFileInfo& file, const uint8_t weight) const {
  std::string path = dir();
  if (weight != 0) path += "/weight-" + std::to_string(weight);
  return path + "/" + stems[file.stem] + "_" + std::to_string(file.pointSize) + ".cpfont";
}

uint8_t SdCardFontFamilyInfo::weights(const SdCardFontFileInfo&) const { return 1; }

const SdCardFontFileInfo* SdCardFontFamilyInfo::findFile(uint8_t size, uint8_t style) const {
  for (const auto& f : files)
    if (f.pointSize == size && f.style == style) return &f;
  return nullptr;
}

const SdCardFontFileInfo* SdCardFontFamilyInfo::findNearestSize(const uint8_t pointSize, const uint8_t style) const {
  const SdCardFontFileInfo* best = nullptr;
  int bestDelta = 256;
  for (const auto& f : files) {
    if (f.style != style) continue;
    const int delta = std::abs(static_cast<int>(f.pointSize) - pointSize);
    if (!best || delta < bestDelta || (delta == bestDelta && f.pointSize < best->pointSize)) {
      best = &f;
      bestDelta = delta;
    }
  }
  return best;
}

bool SdCardFontRegistry::discover() {
  ++hostDiscoveries;
  families_ = hostCatalog;
  return !families_.empty();
}

const SdCardFontFamilyInfo* SdCardFontRegistry::findFamily(const std::string& name) const {
  for (const auto& f : families_)
    if (f.name == name) return &f;
  return nullptr;
}

uint8_t snapToNearestPointSize(const uint8_t* sizes, size_t count, uint8_t pt) {
  uint8_t best = sizes[0];
  for (size_t i = 1; i < count; ++i)
    if (std::abs(sizes[i] - pt) < std::abs(best - pt)) best = sizes[i];
  return best;
}
