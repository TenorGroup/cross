#include "FrontlightGesture.h"

#include <GfxRenderer.h>
#include <cstdio>
#include "fontIds.h"

void FrontlightGesture::draw(const GfxRenderer& r, const int barTop) const {
  if (!visible) return;
  char text[8];
  snprintf(text, sizeof(text), "%u%%", value);
  constexpr int font = SMALL_FONT_ID;
  const bool overlayPlane = r.getRenderMode() != GfxRenderer::BW && !r.grayPlanesAreAbsolute();
  if (vertical) {
    const int x = r.getScreenWidth() - 58;
    const int h = std::min(240, barTop - 64);
    const int y = std::max(32, (barTop - h) / 2);
    r.fillRect(x - 6, y - 28, 58, h + 40, overlayPlane);
    if (overlayPlane) return;
    r.drawText(font, x + 14 - r.getTextWidth(font, text) / 2, y - 24, text);
    r.drawRect(x + 8, y, 12, h);
    const int fill = (h - 4) * value / 100;
    r.fillRect(x + 10, y + h - 2 - fill, 8, fill);
  } else {
    const int width = std::min(280, r.getScreenWidth() - 100);
    const int x = (r.getScreenWidth() - width) / 2;
    const int y = barTop - 48;
    r.fillRect(x - 8, y - 24, width + 16, 46, overlayPlane);
    if (overlayPlane) return;
    r.drawText(font, x + width / 2 - r.getTextWidth(font, text) / 2, y - 22, text);
    r.drawRect(x, y, width, 12);
    r.fillRect(x + 2, y + 2, (width - 4) * value / 100, 8);
  }
}
