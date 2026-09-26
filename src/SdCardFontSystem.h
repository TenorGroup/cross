#pragma once

#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <atomic>
#include <utility>

#include "ReaderInkWeight.h"

class GfxRenderer;

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  SdCardFontSystem() = default;
  SdCardFontSystem(const SdCardFontSystem&) = delete;
  SdCardFontSystem& operator=(const SdCardFontSystem&) = delete;
  /// Discover SD card fonts and load user's saved selection. Call once during setup.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current settings.
  /// Call before entering the reader or after settings change.
  /// Also re-discovers if the registry has been marked dirty (e.g. by web upload).
  void ensureLoaded(GfxRenderer& renderer);
  // Caller holds RenderLock and clears font caches before changing UI faces.
  // Missing auxiliary packs leave that UI alias on its built-in glyphs.
  void refreshUiFallbacks(GfxRenderer& renderer, uint8_t uiTextSize);
  uint8_t availableWeightMask() const;
  uint8_t effectiveWeight() const { return readerInk::publicFromPhysical(manager_.currentWeight()); }

  // OTA uses built-in UI fonts and reboots on exit. Release SD font metadata
  // and the discovery catalog before TLS; settings and card files stay intact.
  // Caller holds RenderLock. ensureLoaded() discovers and loads them again.
  void releaseForOta(GfxRenderer& renderer);

  /// Resolve an SD card font ID from family name + reader point size.
  /// Returns 0 if not found. Used by CrossPointSettings::getReaderFontId().
  int resolveFontId(const char* familyName, uint8_t pointSize) const;

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  /// The boot may leave the catalog unread; the first access reads it.
  const SdCardFontRegistry& registry() const;

  /// Non-const access to the registry (for FontInstaller).
  SdCardFontRegistry& registry() { return const_cast<SdCardFontRegistry&>(std::as_const(*this).registry()); }

  /// Mark the registry as needing re-discovery, and forget the family the next
  /// wake would load without reading the catalog (SdFontBootMemo.h).
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty();

  /// If the registry is dirty, re-scan the SD card now and clear the flag.
  /// Used by the web UI so uploaded/deleted fonts appear in the list
  /// without waiting for the reader activity to run ensureLoaded().
  void refreshIfDirty() {
    if (registryDirty_.exchange(false, std::memory_order_acquire) && !readCatalogIfPending()) {
      walkCatalog();
    }
  }

 private:
  // Catalog states. Pending: the boot left it unread, the first registry use reads it.
  enum : uint8_t { CATALOG_READY, CATALOG_PENDING, CATALOG_READING };
  // Reads the catalog if the boot left it unread; true when this call read it. A second
  // task asking while the first reads waits for it instead of reading a half-built list.
  bool readCatalogIfPending() const;
  // Walks the family folders on the card and keeps what it read for the next wake.
  void walkCatalog() const;
  // After a load failed on a catalog the last boot kept: walks the card once, true when it did
  // (the caller looks the family up again).
  bool walkIfCatalogKept() const;
  // The family called `name`: the one the boot memo described while the catalog is unread.
  const SdCardFontFamilyInfo* familyNamed(const std::string& name) const;
  // Loads `family` at the saved size and weight, remembers it for the next wake and sets up
  // the UI fallbacks. False when its file does not load.
  bool loadSelected(GfxRenderer& renderer, const SdCardFontFamilyInfo& family);

  // Load the active SD family at the built-in UI point sizes and register each
  // as a size-matched CJK fallback for the corresponding UI font, so CJK book
  // titles/list rows render at the same size as the surrounding Latin UI text.
  // No-op when no SD family is loaded. Safe to call repeatedly (sizes already
  // loaded are reused).
  void setupUiFallbacks(GfxRenderer& renderer);

  mutable SdCardFontRegistry registry_;
  mutable std::atomic<uint8_t> catalog_{CATALOG_READY};
  // The catalog came from the RTC memo and has not been checked against the card since.
  mutable bool catalogKept_ = false;
  // The family begin() loaded from the boot memo, answering for the catalog until it is read.
  // Not changed after begin(); an empty name means the boot did not use the memo.
  SdCardFontFamilyInfo bootFamily_;
  SdCardFontManager manager_;
  uint8_t loadedRequestWeight_ = 0;
  uint8_t uiTextSize_ = 0;
  std::atomic<bool> registryDirty_{false};
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
