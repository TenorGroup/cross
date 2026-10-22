#include "FontOwnName.h"

#include <Arduino.h>
#include <HeapFloor.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <Utf8.h>

#include <algorithm>
#include <climits>
#include <string>

namespace fontownname {
namespace {
// What a font takes at its peak (ESTIMATE from the file layout, to be measured with the probe log): the loaded
// file's tables and the warmed glyphs of a name of some twenty letters. Asked of the heap before the load.
constexpr size_t kNeedBytes = 12 * 1024, kBiggestBytes = 6 * 1024;

bool onFloor() { return !heapfloor::crosses(ESP.getFreeHeap(), ESP.getMaxAllocHeap()); }
}  // namespace

bool render(const char* path, const char* name, const int maxWidth, const int maxHeight, Picture& out,
            const Accept accept, void* const ctx) {
  if (!path || !name || !*name || maxWidth <= 0 || maxHeight <= 0) return false;
#ifdef TENOR_PRESS_PROBE
  const unsigned long started = millis();
  const uint32_t freeBefore = ESP.getFreeHeap(), largestBefore = ESP.getMaxAllocHeap();
  const SdCardFont* probed = nullptr;
  unsigned long loadMs = 0;
  const char* why = "drawn";
  const Picture* made = nullptr;
  const auto log = [&](const bool ok) {
    const SdCardFont::Stats stats = probed ? probed->getStats() : SdCardFont::Stats{};
    LOG_INF("FONT", "own-name %s ok=%d why=%s ms=%lu load=%lu sdReadMs=%u seeks=%u prewarmMs=%u glyphs=%u bitmapB=%u "
                    "pic=%ux%u/%uB heap free=%u->%u largest=%u->%u",
            path, ok ? 1 : 0, why, millis() - started, loadMs, static_cast<unsigned>(stats.sdReadTimeMs),
            static_cast<unsigned>(stats.seekCount), static_cast<unsigned>(stats.prewarmTotalMs),
            static_cast<unsigned>(stats.uniqueGlyphs), static_cast<unsigned>(stats.bitmapBytes),
            made ? made->width : 0u, made ? made->height : 0u, made ? static_cast<unsigned>(made->size) : 0u,
            static_cast<unsigned>(freeBefore), static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(largestBefore), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    return ok;
  };
#define OWN_NAME_FAIL(reason) \
  do {                        \
    why = reason;             \
    return log(false);        \
  } while (0)
#else
#define OWN_NAME_FAIL(reason) return false
#endif
  if (!heapfloor::leaves(kNeedBytes, kBiggestBytes, ESP.getFreeHeap(), ESP.getMaxAllocHeap())) OWN_NAME_FAIL("no-heap");
  std::unique_ptr<SdCardFont> font(new (std::nothrow) SdCardFont());
  if (!font) OWN_NAME_FAIL("alloc");
#ifdef TENOR_PRESS_PROBE
  probed = font.get();
#endif
  if (!onFloor()) OWN_NAME_FAIL("floor-alloc");
  // Light load: the regular style only and no content hash (a layout-cache key this label never needs), which is
  // almost all the time a full load takes.
  const bool loaded = font->load(path, SdCardFont::LoadOptions{true, 0x01});
#ifdef TENOR_PRESS_PROBE
  loadMs = millis() - started;
#endif
  if (!loaded || !font->hasStyle(0)) OWN_NAME_FAIL("load");
  if (!onFloor()) OWN_NAME_FAIL("floor-load");
  const EpdFont* regular = font->getEpdFont(0);
  if (!regular) OWN_NAME_FAIL("no-style");
  // Every letter of the name must be in the file: a missing one would draw as a box, or a fallback face's glyph. A space
  // is a gap, never a letter.
  bool hasSpace = false, hasX = false;
  for (const unsigned char* at = reinterpret_cast<const unsigned char*>(name); *at;) {
    const uint32_t cp = utf8NextCodepoint(&at);
    hasSpace |= cp == ' ';
    hasX |= cp == 'x';
    if (cp != ' ' && !regular->hasCodepoint(cp)) OWN_NAME_FAIL("glyph");
  }
  // prewarm() returns how many of the codepoints it was asked for the file lacks. It is asked for the name, an x (the
  // x-height tells which size of the folder stands nearest the UI face) and, always, U+FFFD: a pack without that glyph (Geist,
  // BeVietnamPro) counts it as one miss. What it may miss is exactly what the checks above let through; more is a
  // letter that did not load.
  const int absent = !regular->hasCodepoint(REPLACEMENT_GLYPH) + (hasSpace && !regular->hasCodepoint(' ')) +
                     (!hasX && !regular->hasCodepoint('x'));
  // Regular style only, no kern or ligature tables: a short label.
  const int missed = font->prewarm((std::string(name) + 'x').c_str(), 0x01, false, false);
  if (missed < 0 || missed > absent) OWN_NAME_FAIL("prewarm");
  if (!onFloor()) OWN_NAME_FAIL("floor-prewarm");
  const EpdGlyph* ex = regular->hasCodepoint('x') ? regular->getGlyph('x') : nullptr;
  if (accept && !accept(ctx, ex ? ex->height : 0)) {
    out.xHeight = ex ? ex->height : 0;
    OWN_NAME_FAIL("other-size");
  }

  // The glyphs in place: left to right along the baseline, each step the last glyph's advance snapped to a pixel as the
  // renderer does (no kern: none was loaded). Calls each(x, glyph) for a glyph with ink; false from it stops the walk
  // before that glyph. Returns how many codepoints it passed.
  const EpdFontData* data = regular->data;
  const auto walk = [&](const size_t limit, const auto& each) {
    size_t passed = 0;
    int x = 0;
    int32_t advance = 0;
    for (const unsigned char* at = reinterpret_cast<const unsigned char*>(name); *at && passed < limit; ++passed) {
      const uint32_t cp = utf8NextCodepoint(&at);
      const EpdGlyph* glyph = regular->hasCodepoint(cp) ? regular->getGlyph(cp) : nullptr;
      x += fp4::toPixel(advance);
      advance = glyph ? glyph->advanceX : (cp == ' ' ? fp4::fromPixel(data->advanceY / 4) : 0);
      if (glyph && glyph->width && glyph->height && !each(x, *glyph)) break;
    }
    return passed;
  };
  int minX = INT_MAX, maxX = INT_MIN, above = 0, below = 0;
  const size_t passed = walk(SIZE_MAX, [&](const int x, const EpdGlyph& glyph) {
    const int left = std::min(minX, x + glyph.left), right = std::max(maxX, x + glyph.left + glyph.width);
    if (right - left > maxWidth) return false;  // the rest is cut
    minX = left;
    maxX = right;
    above = std::max<int>(above, glyph.top);
    below = std::max<int>(below, glyph.height - glyph.top);
    return true;
  });
  if (maxX <= minX) OWN_NAME_FAIL("blank");
  const int width = maxX - minX, height = above + below;
  if (height > maxHeight) OWN_NAME_FAIL("tall");
  const size_t rowBytes = static_cast<size_t>(width + 7) / 8, size = rowBytes * height;
  std::unique_ptr<uint8_t[]> bits(new (std::nothrow) uint8_t[size]());
  if (!bits) OWN_NAME_FAIL("alloc-bits");
  if (!onFloor()) OWN_NAME_FAIL("floor-bits");
  walk(passed, [&](const int x, const EpdGlyph& glyph) {
    if (!glyph.dataLength) return true;
    const uint8_t* bitmap = font->isOverflowGlyph(&glyph) ? font->getOverflowBitmap(&glyph) : data->bitmap + glyph.dataOffset;
    if (!bitmap) return true;
    for (int row = 0; row < glyph.height; ++row) {
      for (int col = 0; col < glyph.width; ++col) {
        const int at = row * glyph.width + col;
        // 2 bits a pixel, 0 = white: the first level of grey and up is ink, as in the store's pictures.
        if (!(data->is2Bit ? (bitmap[at >> 2] >> ((3 - (at & 3)) * 2)) & 3 : (bitmap[at >> 3] >> (7 - (at & 7))) & 1)) continue;
        const int px = x + glyph.left - minX + col;
        bits[(above - glyph.top + row) * rowBytes + px / 8] |= 0x80 >> (px % 8);
      }
    }
    return true;
  });
  out.bits = std::move(bits);
  out.size = size;
  out.width = static_cast<uint16_t>(width);
  out.height = static_cast<uint16_t>(height);
  out.baseline = static_cast<int16_t>(above);
  out.xHeight = ex ? ex->height : 0;
#ifdef TENOR_PRESS_PROBE
  made = &out;
  return log(true);
#else
  return true;
#endif
}

#undef OWN_NAME_FAIL

}  // namespace fontownname
