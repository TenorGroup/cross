#include "SdCardFontManager.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>

SdCardFontManager::~SdCardFontManager() {
  for (auto& lf : loaded_) {
    delete lf.font;
  }
}

// FNV-1a continuation: seeds with contentHash, then hashes family name + point size.
// Produces a deterministic ID that is stable across load/unload cycles and reboots,
// and changes when font content changes (different header/TOC = different contentHash).
int SdCardFontManager::computeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize, uint8_t weight) {
  static constexpr uint32_t FNV_PRIME = 16777619u;
  uint32_t hash = contentHash;
  while (*familyName) {
    hash ^= static_cast<uint8_t>(*familyName++);
    hash *= FNV_PRIME;
  }
  hash ^= pointSize;
  hash *= FNV_PRIME;
  if (weight) {
    hash ^= 0x57475431u;  // outline recipe namespace v1; keep all base IDs unchanged
    hash *= FNV_PRIME;
    hash ^= weight;
    hash *= FNV_PRIME;
  }
  int id = static_cast<int>(hash);
  return id != 0 ? id : 1;  // 0 is reserved as "not found" sentinel
}

uint8_t SdCardFontManager::selectWeight(const uint8_t availableMask, const uint8_t requested) {
  if (requested == 0) return 0;
  if (requested == 1) return (availableMask & (1u << 1)) ? 1 : 0;
  if (requested > 4) return 0;
  for (int weight = requested; weight >= 2; --weight) {
    if (availableMask & (1u << weight)) return static_cast<uint8_t>(weight);
  }
  // weight-1 belongs to the old pack format. Keep it usable when the pack has
  // no current-format variant, while never selecting a stronger level.
  if ((availableMask & 0x1cu) == 0 && (availableMask & (1u << 1))) return 1;
  return 0;
}

int SdCardFontManager::loadFile(const SdCardFontFamilyInfo& family, const SdCardFontFileInfo& file,
                                GfxRenderer& renderer, uint8_t weight) {
  const std::string path = family.filePath(file, weight);
  const char* familyName = family.name.c_str();
  auto* font = new (std::nothrow) SdCardFont();
  if (!font) {
    LOG_ERR("SDMGR", "Failed to allocate SdCardFont for %s", path.c_str());
    return 0;
  }

  if (!font->load(path.c_str())) {
    LOG_ERR("SDMGR", "Failed to load %s", path.c_str());
    delete font;
    return 0;
  }

  int fontId = computeFontId(font->contentHash(), familyName, file.pointSize, weight);
  // Guard against collision with built-in font IDs (astronomically unlikely
  // with FNV-1a hashes, but provides a safety net)
  if (renderer.getFontMap().count(fontId) != 0) {
    LOG_ERR("SDMGR", "Font ID %d collides with existing font, skipping %s", fontId, path.c_str());
    delete font;
    return 0;
  }
  renderer.registerSdCardFont(fontId, font);
  loaded_.push_back({font, fontId, file.pointSize, weight});

  LOG_DBG("SDMGR", "Loaded %s size=%u id=%d styles=%u", path.c_str(), file.pointSize, fontId, font->styleCount());

  EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
  renderer.insertFont(fontId, fontFamily);
  return fontId;
}

bool SdCardFontManager::loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize,
                                   uint8_t weight) {
  const SdCardFontFileInfo* selected = family.findNearestSize(pointSize);
  if (!selected) {
    if (!loadedFamilyName_.empty()) unloadAll(renderer);
    LOG_ERR("SDMGR", "Family %s has no files to load", family.name.c_str());
    return false;
  }

  const uint8_t available = family.weights(*selected);
  uint8_t effectiveWeight = selectWeight(available, weight);
  if (!loadedFamilyName_.empty()) {
    unloadAll(renderer);
  }

  loaded_.reserve(4);  // reader plus up to three UI fallback sizes
  int id = 0;
  for (int candidate = effectiveWeight; candidate >= 2 && id == 0; --candidate) {
    if (available & (1u << candidate)) {
      id = loadFile(family, *selected, renderer, static_cast<uint8_t>(candidate));
      if (id != 0) effectiveWeight = static_cast<uint8_t>(candidate);
    }
  }
  if (id == 0 && effectiveWeight == 1) id = loadFile(family, *selected, renderer, 1);
  if (id == 0) {
    effectiveWeight = 0;
    id = loadFile(family, *selected, renderer);
  }
  if (id == 0) return false;
  loadedWeight_ = effectiveWeight;

  loadedFamilyName_ = family.name;
  loadedPointSize_ = selected->pointSize;
  return true;
}

int SdCardFontManager::loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                           uint8_t pointSize) {
  const SdCardFontFileInfo* file = family.findFile(pointSize);
  if (!file) return 0;  // family has no .cpfont at this exact size

  // Reuse an already-loaded font of the same size (e.g. when a reader size
  // happens to match a UI size) instead of double-loading the file.
  for (const auto& lf : loaded_) {
    if (lf.size == pointSize && lf.weight == 0) return lf.fontId;
  }

  return loadFile(family, *file, renderer);
}

void SdCardFontManager::unloadExtraSizes(GfxRenderer& renderer) {
  // Clear mappings before deleting their objects. The first entry is always
  // the reader font; a UI alias may have reused it and it stays registered.
  renderer.clearFallbackFonts();
  while (loaded_.size() > 1) {
    auto& extra = loaded_.back();
    renderer.removeFont(extra.fontId);
    delete extra.font;
    loaded_.pop_back();
  }
}

void SdCardFontManager::unloadAll(GfxRenderer& renderer) {
  // Drop UI CJK fallbacks before the SD fonts they point at are freed.
  renderer.clearFallbackFonts();
  renderer.clearSdCardFonts();
  for (auto& lf : loaded_) {
    renderer.removeFont(lf.fontId);
    delete lf.font;
  }
  loaded_.clear();
  loadedFamilyName_.clear();
  loadedPointSize_ = 0;
  loadedWeight_ = 0;
}

int SdCardFontManager::getFontId(const std::string& familyName) const {
  if (familyName != loadedFamilyName_ || loaded_.empty()) return 0;
  return loaded_.front().fontId;
}
