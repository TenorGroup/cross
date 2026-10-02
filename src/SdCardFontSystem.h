#pragma once

#include <HalStorage.h>  // HalFile (kept open for streamed TTFs)
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <VectorFontSupport.h>

#if CROSSPOINT_VECTOR_FONTS
#include <FontPsram.h>  // PsramVector for resident TTF bytes
#endif

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ReaderInkWeight.h"

class GfxRenderer;
class TtfEpdFont;

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  // Constructor and destructor are out-of-line (defined in the .cpp where
  // TtfEpdFont is a complete type) so the std::unique_ptr<TtfEpdFont> member
  // can be constructed/destroyed with only a forward declaration visible here.
  SdCardFontSystem();
  ~SdCardFontSystem();
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
  // Frees the catalog list when the RTC memo can bring it back: the next registry() restores it
  // without walking the card. False when it stays (unread, fonts changed, or not kept).
  bool releaseCatalog();

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
  // as a size-matched script fallback for the corresponding UI font, so book
  // titles/list rows in scripts the built-ins lack (CJK, Greek, Cyrillic, ...)
  // render at the same size as the surrounding Latin UI text. No-op when no SD
  // family is loaded. Safe to call repeatedly (sizes already loaded are
  // reused).
  void setupUiFallbacks(GfxRenderer& renderer);

#if CROSSPOINT_VECTOR_FONTS
  // --- Vector (.ttf/.otf) font path (FreeInkFont via TtfEpdFont) -------------
  // Load/refresh the selected TTF family at the current reader size, register
  // it with the renderer, and track it so ensureSdCardFontReady() rebuilds its
  // glyph set per page. registryWasDirty forces a reload even if unchanged.
  void loadTtfFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, bool registryWasDirty);
  // Unregister + free the active TTF font (and its UI-size fallbacks), if any.
  void unloadTtf(GfxRenderer& renderer);
  // Register the loaded TTF at each built-in UI size as a script fallback, so UI
  // text (book titles, list rows, menus, status bar) in scripts the built-in
  // fonts lack renders in the chosen TTF. Mirrors setupUiFallbacks for .cpfont.
  void setupTtfUiFallbacks(GfxRenderer& renderer);
  // Open one style source file (resident if small, streamed if large) into
  // ttfSources_[style]. Returns false on open/read failure.
  bool openTtfSource(uint8_t style, const std::string& path);
  // Register every present source with `font` (shared bytes / file handles).
  void addTtfSources(TtfEpdFont& font);
  // Close/free all style sources.
  void freeTtfSources();
  // ReadFn for streamed sources: serves the PSRAM prefix cache first, SD after.
  static unsigned long prefixRead(void* ctx, unsigned long offset, unsigned char* buffer, unsigned long count);
#endif  // CROSSPOINT_VECTOR_FONTS

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

#if CROSSPOINT_VECTOR_FONTS
  // One style source file. SMALL files are read fully into `bytes` (resident,
  // PSRAM when present); LARGE files stream from `file` (kept open) so a multi-MB
  // file never sits in RAM. All faces (reader + UI sizes) share these sources.
  struct TtfSource {
    // Resident form: the whole file. Streamed form: a PSRAM prefix cache of the
    // file head (cmap/loca/hmtx) — empty when PSRAM couldn't fund it.
    freeink::font::PsramVector<uint8_t> bytes;
    HalFile file;  // open handle (streamed form)
    bool streamed = false;
    unsigned long size = 0;
    bool present = false;
  };

  // Active TTF font (at most one reader-size vector family loaded at a time).
  std::unique_ptr<TtfEpdFont> ttf_;
  // Up to 4 style sources: 0=regular (required), 1=bold, 2=italic, 3=bold-italic.
  TtfSource ttfSources_[4];
  std::string ttfFamily_;     // loaded vector family name ("" = none)
  int ttfFontId_ = 0;         // renderer font id for ttf_ (0 = none)
  uint8_t ttfPointSize_ = 0;  // size ttf_ was built at
  // UI-size TTF fallbacks (share ttfSources_); parallel to their renderer font ids.
  std::vector<std::unique_ptr<TtfEpdFont>> ttfUi_;
  std::vector<int> ttfUiIds_;
#endif  // CROSSPOINT_VECTOR_FONTS
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
