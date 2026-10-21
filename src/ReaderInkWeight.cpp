#include "ReaderInkWeight.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"

void readerInk::apply(GfxRenderer& renderer) {
  sdFontSystem.ensureLoadedImpl(renderer);
  // The font draws at the nearest level its pack has (a font made with few levels has no +2..+5). A choice must
  // never be kept while another level is drawn: the setting follows what the font really has, so the screen
  // and the row say the same thing. The Noto pack missing is a different state (the row tells to download it).
  if (!sdFontSystem.needsNotoPack()) {
    const uint8_t used = sdFontSystem.effectiveWeight();
    if (used != clamp(SETTINGS.readerInkWeight)) SETTINGS.readerInkWeight = used;
  }
  if (auto* manager = renderer.getFontCacheManager())
    manager->setReaderInk(SETTINGS.getReaderFontId(), outlineStrength(SETTINGS.readerInkWeight),
                          SETTINGS.textAntiAliasing);
}
