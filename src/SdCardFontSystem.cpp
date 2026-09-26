#include "SdCardFontSystem.h"

#include <Arduino.h>
#include <HalMemory.h>
#ifndef SIMULATOR
#include <esp_system.h>
#endif

#include <GfxRenderer.h>
#include <Logging.h>

#include <iterator>

#include "CrossPointSettings.h"
#include "ReaderFontSizes.h"
#include "ReaderInkWeight.h"
#include "SdFontBootMemo.h"
#include "components/UIScale.h"
#include "fontIds.h"

namespace {

// Survives deep sleep, not power loss (SdFontBootMemo.h).
RTC_NOINIT_ATTR sdfontmemo::Memo bootMemo;
RTC_NOINIT_ATTR sdfontmemo::Catalog catalogMemo;

bool wokeFromDeepSleep() {
#ifdef SIMULATOR
  return false;  // the simulator wakes as a new process: no RTC memory survives its sleep
#else
  return esp_reset_reason() == ESP_RST_DEEPSLEEP;
#endif
}

void snapFontPointSizeTo(const uint8_t availablePointSize) {
  if (availablePointSize == 0 || availablePointSize == SETTINGS.fontPointSize) return;
  LOG_DBG("SDFS", "Font size %u unavailable, snapping to %u", SETTINGS.fontPointSize, availablePointSize);
  SETTINGS.fontPointSize = availablePointSize;
  SETTINGS.saveToFile();
}

// Built-in UI fonts and their physical point sizes (at 150 DPI, matching the
// SD-font converter). Each is paired with a same-size SD fallback so CJK UI
// text matches the surrounding Latin. See SdCardFontSystem::setupUiFallbacks.
struct UiFontSize {
  int fontId;
  uint8_t pointSize;
};

}  // namespace

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t pointSize) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, pointSize);
  };
  SETTINGS.sdFontResolverCtx = this;

  // The first screen needs no font catalog beyond the saved family, so the walk over every
  // family folder waits for its first use (registry()). A wake from deep sleep finds the saved
  // family in the memo the boot before left; any other boot, or a memo whose file no longer
  // loads, walks the card now, as before.
  // The catalog itself comes back from the memo as well when the last walk fit in it.
  catalog_.store(CATALOG_PENDING, std::memory_order_release);
  const bool wake = wokeFromDeepSleep();
  std::vector<SdCardFontFamilyInfo> kept;
  if (sdfontmemo::restoreCatalog(catalogMemo, wake, kept)) {
    registry_.adopt(std::move(kept));
    catalogKept_ = true;
    catalog_.store(CATALOG_READY, std::memory_order_release);
    LOG_PROBE("SDFS", "Catalog kept from sleep: families=%d", registry_.getFamilyCount());
  } else {
    catalogMemo.magic = 0;  // a restart keeps RTC memory: a later wake must not trust an old catalog
  }
  const char* saved = SETTINGS.sdFontFamilyName;
  const bool fromMemo = saved[0] != '\0' && sdfontmemo::restore(bootMemo, wake, saved, bootFamily_);
  bootMemo = sdfontmemo::Memo{};  // rewritten below by a load that succeeds
  if (saved[0] != '\0' && !(fromMemo && loadSelected(renderer, bootFamily_))) {
    bootFamily_.name.clear();  // familyNamed() no longer answers from the memo
    readCatalogIfPending();
    const auto* family = registry_.findFamily(saved);
    bool loaded = family && loadSelected(renderer, *family);
    if (!loaded && walkIfCatalogKept()) {
      family = registry_.findFamily(saved);
      loaded = family && loadSelected(renderer, *family);
    }
    if (!family) {
      LOG_DBG("SDFS", "SD font family not found on card: %s (clearing)", saved);
      SETTINGS.clearSdFontFamily();
    } else if (!loaded) {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", saved);
      SETTINGS.clearSdFontFamily();
    }
  }
}

void SdCardFontSystem::markRegistryDirty() {
  bootMemo = sdfontmemo::Memo{};
  catalogMemo.magic = 0;
  registryDirty_.store(true, std::memory_order_release);
}

const SdCardFontRegistry& SdCardFontSystem::registry() const {
  readCatalogIfPending();
  return registry_;
}

bool SdCardFontSystem::readCatalogIfPending() const {
  uint8_t expected = CATALOG_PENDING;
  if (catalog_.compare_exchange_strong(expected, CATALOG_READING, std::memory_order_acq_rel)) {
    walkCatalog();
    const auto heap = HalMemory::getInternalHeap();
    LOG_INF("HEAP", "fonts-sd-registry free=%u largest=%u families=%d", static_cast<unsigned>(heap.freeBytes),
            static_cast<unsigned>(heap.largestBlockBytes), registry_.getFamilyCount());
    catalog_.store(CATALOG_READY, std::memory_order_release);
    return true;
  }
  while (catalog_.load(std::memory_order_acquire) == CATALOG_READING) delay(1);
  return false;
}

void SdCardFontSystem::walkCatalog() const {
  [[maybe_unused]] const unsigned long started = millis();
  registry_.discover();
  catalogKept_ = false;
  // Fonts changed during the walk (web task) leave the flag set: that catalog is not kept.
  const bool kept = sdfontmemo::saveCatalog(registry_.getFamilies(), catalogMemo) &&
                    !registryDirty_.load(std::memory_order_acquire);
  if (!kept) catalogMemo.magic = 0;
  LOG_PROBE("SDFS", "Catalog walk: families=%d ms=%lu kept=%d", registry_.getFamilyCount(), millis() - started,
            kept ? 1 : 0);
}

bool SdCardFontSystem::walkIfCatalogKept() const {
  if (!catalogKept_) return false;
  LOG_PROBE("SDFS", "Kept catalog does not load, walking the card");
  walkCatalog();
  return true;
}

const SdCardFontFamilyInfo* SdCardFontSystem::familyNamed(const std::string& name) const {
  if (catalog_.load(std::memory_order_acquire) == CATALOG_PENDING && !bootFamily_.name.empty() &&
      bootFamily_.name == name) {
    return &bootFamily_;
  }
  readCatalogIfPending();
  return registry_.findFamily(name);
}

bool SdCardFontSystem::loadSelected(GfxRenderer& renderer, const SdCardFontFamilyInfo& family) {
  if (!manager_.loadFamily(family, renderer, SETTINGS.fontPointSize, readerInk::physical(SETTINGS.readerInkWeight))) {
    return false;
  }
  snapFontPointSizeTo(manager_.currentPointSize());
  loadedRequestWeight_ = SETTINGS.readerInkWeight;
  sdfontmemo::save(family, bootMemo);
  setupUiFallbacks(renderer);
  LOG_DBG("SDFS", "Loaded SD font family: %s", family.name.c_str());
  return true;
}

void SdCardFontSystem::releaseForOta(GfxRenderer& renderer) {
  manager_.unloadAll(renderer);
  registry_ = SdCardFontRegistry{};
  catalog_.store(CATALOG_READY, std::memory_order_release);  // an unread catalog stays unread
  registryDirty_.store(true, std::memory_order_release);
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size - the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  if (registryWasDirty) {
    LOG_DBG("SDFS", "Registry dirty — re-discovering fonts");
    if (!readCatalogIfPending()) walkCatalog();
  }

  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    // Back on a built-in family, which exists only at BUILTIN_READER_POINT_SIZES:
    // a size inherited from an SD family has to come back into that set.
    snapFontPointSizeTo(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES),
                                               SETTINGS.fontPointSize));
    return;
  }

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = familyNamed(wantedFamily);
    if (!family && walkIfCatalogKept()) family = familyNamed(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.clearSdFontFamily();
      return;
    }
    const auto* selected = family->findNearestSize(SETTINGS.fontPointSize);
    const uint8_t wantedPt = selected ? selected->pointSize : 0;
    // Snap before the early return: the wanted size can already be loaded while
    // the setting still names a size this family does not ship.
    snapFontPointSizeTo(wantedPt);
    // Cache the request as well as the effective weight: a missing or damaged
    // variant falls back once and must not be reopened on every preview.
    if (!registryWasDirty && wantedPt == manager_.currentPointSize() &&
        SETTINGS.readerInkWeight == loadedRequestWeight_)
      return;
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u%s", wantedFamily, manager_.currentPointSize(), wantedPt,
            registryWasDirty ? " [registry dirty]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = familyNamed(wantedFamily);
  bool loaded = family && loadSelected(renderer, *family);
  if (!loaded && walkIfCatalogKept()) {
    family = familyNamed(wantedFamily);
    loaded = family && loadSelected(renderer, *family);
  }
  if (family) {
    if (!loaded) {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.clearSdFontFamily();
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.clearSdFontFamily();
  }
}

void SdCardFontSystem::refreshUiFallbacks(GfxRenderer& renderer, uint8_t uiTextSize) {
  uiTextSize_ = normalizedUiTextSize(uiTextSize);
  manager_.unloadExtraSizes(renderer);
  setupUiFallbacks(renderer);
}

void SdCardFontSystem::setupUiFallbacks(GfxRenderer& renderer) {
  const std::string& familyName = manager_.currentFamilyName();
  if (familyName.empty()) return;  // no SD family loaded — nothing to fall back to

  const auto* family = familyNamed(familyName);
  if (!family) return;

  // Probe the already-loaded reader-size font before paying for the UI sizes:
  // resolveTextFontId only redirects on CJK codepoints, so a Latin-only family
  // can never act as a fallback and its UI sizes would be dead weight in RAM.
  const auto readerIt = renderer.getFontMap().find(manager_.getFontId(familyName));
  if (readerIt == renderer.getFontMap().end()) return;
  // One representative codepoint per script: Han, Hiragana, Katakana, Hangul.
  static constexpr uint32_t kCjkProbes[] = {0x4E00, 0x3042, 0x30A2, 0xAC00};
  bool hasCjk = false;
  for (const uint32_t cp : kCjkProbes) {
    if (readerIt->second.hasCodepoint(cp)) {
      hasCjk = true;
      break;
    }
  }
  if (!hasCjk) {
    LOG_DBG("SDFS", "%s has no CJK coverage - skipping UI fallback sizes", familyName.c_str());
    return;
  }

  const auto spec = uiTextSizeSpec(uiTextSize_);
  const UiFontSize sizes[] = {{SMALL_FONT_ID, spec.captionPointSize},
                              {UI_10_FONT_ID, spec.subtitlePointSize},
                              {UI_12_FONT_ID, spec.bodyPointSize}};
  for (const auto& ui : sizes) {
    const int sdFontId = manager_.loadFamilyExtraSize(*family, renderer, ui.pointSize);
    if (sdFontId != 0) {
      renderer.setFallbackFont(ui.fontId, sdFontId);
    } else {
      LOG_DBG("SDFS", "No %u pt SD glyphs for UI fallback in %s", ui.pointSize, familyName.c_str());
    }
  }
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*pointSize*/) const {
  // The manager holds exactly one reader-size font, already selected for
  // SETTINGS.fontPointSize, so the size argument is implicit - always return
  // that font's ID. ensureLoaded() must have run for the current settings first.
  return manager_.getFontId(familyName);
}

uint8_t SdCardFontSystem::availableWeightMask() const {
  const auto* family = familyNamed(SETTINGS.sdFontFamilyName);
  const auto* file = family ? family->findNearestSize(SETTINGS.fontPointSize) : nullptr;
  return file ? readerInk::publicMask(family->weights(*file)) : 1;
}
