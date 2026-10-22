#include "FontOwnName.h"

#include <Arduino.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HeapFloor.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>
#include <Utf8.h>

#include <memory>
#include <string>

namespace fontownname {
namespace {
// One id for the one font drawn at a time: it is registered for the draw and removed after it.
constexpr int kFontId = 0x4F574E31;
// What a font takes at its peak (ESTIMATE from the file layout, to be measured with the probe log): the loaded
// file's tables and the warmed glyphs of a name of some twenty letters. Asked of the heap before the load.
constexpr size_t kNeedBytes = 12 * 1024, kBiggestBytes = 6 * 1024;

bool onFloor() { return !heapfloor::crosses(ESP.getFreeHeap(), ESP.getMaxAllocHeap()); }

// Takes the font out of the renderer before the file is deleted.
struct Registered {
  GfxRenderer& renderer;
  bool on = false;
  ~Registered() {
    if (on) renderer.removeFont(kFontId);
  }
};
}  // namespace

bool draw(GfxRenderer& renderer, const SdCardFontFamilyInfo& family, const char* name, const uint8_t pointSize,
          const int x, const int baselineY, const int maxWidth, const bool black) {
  if (family.vector || !name || !*name || maxWidth <= 0) return false;
  const SdCardFontFileInfo* file = family.findNearestSize(pointSize);
  if (!file) return false;
#ifdef TENOR_PRESS_PROBE
  const unsigned long started = millis();
  const uint32_t freeBefore = ESP.getFreeHeap(), largestBefore = ESP.getMaxAllocHeap();
  const SdCardFont* probed = nullptr;
  const char* why = "drawn";
  const auto log = [&](const bool ok) {
    const SdCardFont::Stats stats = probed ? probed->getStats() : SdCardFont::Stats{};
    LOG_INF("FONT", "own-name %s ok=%d why=%s pt=%u ms=%lu sdReadMs=%u seeks=%u prewarmMs=%u glyphs=%u bitmapB=%u "
                    "heap free=%u->%u largest=%u->%u",
            family.name.c_str(), ok ? 1 : 0, why, static_cast<unsigned>(file->pointSize), millis() - started,
            static_cast<unsigned>(stats.sdReadTimeMs), static_cast<unsigned>(stats.seekCount),
            static_cast<unsigned>(stats.prewarmTotalMs), static_cast<unsigned>(stats.uniqueGlyphs),
            static_cast<unsigned>(stats.bitmapBytes), static_cast<unsigned>(freeBefore),
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(largestBefore),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
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
  if (renderer.getFontMap().count(kFontId)) OWN_NAME_FAIL("id-taken");
  std::unique_ptr<SdCardFont> font(new (std::nothrow) SdCardFont());
  if (!font) OWN_NAME_FAIL("alloc");
#ifdef TENOR_PRESS_PROBE
  probed = font.get();
#endif
  if (!onFloor()) OWN_NAME_FAIL("floor-alloc");
  if (!font->load(family.filePath(*file).c_str()) || !font->hasStyle(0)) OWN_NAME_FAIL("load");
  if (!onFloor()) OWN_NAME_FAIL("floor-load");
  // Every letter of the name must be in the file: a missing one would draw as a box, or a fallback face's glyph.
  const EpdFont* regular = font->getEpdFont(0);
  if (!regular) OWN_NAME_FAIL("no-style");
  for (const unsigned char* at = reinterpret_cast<const unsigned char*>(name); *at;) {
    const uint32_t cp = utf8NextCodepoint(&at);
    if (cp != ' ' && !regular->hasCodepoint(cp)) OWN_NAME_FAIL("glyph");
  }
  // Regular style only, no kern or ligature tables: a short label.
  if (font->prewarm(name, 0x01, false, false) != 0) OWN_NAME_FAIL("prewarm");
  if (!onFloor()) OWN_NAME_FAIL("floor-prewarm");
  Registered registered{renderer};
  renderer.registerSdCardFont(kFontId, font.get());
  registered.on = true;
  renderer.insertFont(kFontId, EpdFontFamily(regular));
  if (!onFloor()) OWN_NAME_FAIL("floor-insert");
  const std::string text = renderer.truncatedText(kFontId, name, maxWidth, EpdFontFamily::REGULAR);
  renderer.drawText(kFontId, x, baselineY - renderer.getFontAscenderSize(kFontId), text.c_str(), black);
#ifdef TENOR_PRESS_PROBE
  return log(true);
#else
  return true;
#endif
}

#undef OWN_NAME_FAIL

}  // namespace fontownname
