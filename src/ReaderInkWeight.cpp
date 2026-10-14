#include "ReaderInkWeight.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"

void readerInk::apply(GfxRenderer& renderer) {
  sdFontSystem.ensureLoadedImpl(renderer);
  if (auto* manager = renderer.getFontCacheManager())
    manager->setReaderInk(SETTINGS.getReaderFontId(), outlineStrength(SETTINGS.readerInkWeight),
                          SETTINGS.textAntiAliasing);
}
