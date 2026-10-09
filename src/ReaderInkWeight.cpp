#include "ReaderInkWeight.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>

#include "CrossPointSettings.h"

void readerInk::apply(GfxRenderer& renderer) {
  if (auto* manager = renderer.getFontCacheManager())
    manager->setReaderInk(SETTINGS.getReaderFontId(), clamp(SETTINGS.readerInkWeight), SETTINGS.textAntiAliasing);
}
