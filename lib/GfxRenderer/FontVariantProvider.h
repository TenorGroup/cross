#pragma once

#include <cmath>

// Sized variants of the reader font (built-in, installed .cpfont, or vector).
// resolve returns a registered font id, 0 when there is none to have (the text keeps the book's font), or REFUSED
// when the size exists but the device cannot hold it now: a layout made at the body size instead would break
// differently from one made with the memory to spare, so the caller must not lay the text out at all.
// load re-registers a cached id after eviction or reboot, returning false when unavailable.
struct FontVariantProvider {
  static constexpr int REFUSED = -1;
  // What a font system with no room for a size answers. A board with PSRAM refuses (it has the memory, so a
  // size it cannot load now is the heap's doing and the chapter is not built); one without keeps the book's font
  // (it has no memory for a second size, always). FREEINK_SIZES_NEED_ROOM builds the first answer on a host.
#if defined(BOARD_HAS_PSRAM) || defined(FREEINK_SIZES_NEED_ROOM)
  static constexpr int NO_ROOM = REFUSED;
#else
  static constexpr int NO_ROOM = 0;
#endif

  void* ctx = nullptr;
  int (*resolve)(void* ctx, int fontId, float scale) = nullptr;
  bool (*load)(void* ctx, int fontId) = nullptr;

  int resolveFont(const int fontId, const float scale) const {
    // Keep small CSS size adjustments from selecting a different font size.
    if (std::fabs(scale - 1.0f) < 0.05f) return fontId;
    return resolve ? resolve(ctx, fontId, scale) : 0;
  }
};
