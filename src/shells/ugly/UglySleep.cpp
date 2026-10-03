#include "UglySleep.h"

#include <I18n.h>

#include "ReadingStatsStore.h"
#include "UglyArt.h"
#include "UglyInk.h"
#include "UglyLogic.h"
#include "components/X3BrandCodec.h"

namespace ugly {
namespace {
constexpr StrId LINES[] = {StrId::STR_UGLY_SLEEP_1, StrId::STR_UGLY_SLEEP_2, StrId::STR_UGLY_SLEEP_3, StrId::STR_UGLY_SLEEP_4,
                           StrId::STR_UGLY_SLEEP_5};
}

bool drawSleep(GfxRenderer& renderer) {
  if (!renderer.hasFrameBuffer() || renderer.getBufferSize() != logic::FRAME_BYTES || renderer.getScreenWidth() != logic::FRAME_W ||
      renderer.getScreenHeight() != logic::FRAME_H)
    return false;
  ensureFonts(renderer);
  if (!decodeX3BrandPlane(art::SLEEP, sizeof(art::SLEEP), renderer.getFrameBuffer(), renderer.getBufferSize())) return false;
  const int pick = logic::sleepLine(ReadingStatsStore::currentDay(), static_cast<int>(sizeof(LINES) / sizeof(LINES[0])));
  paragraph(renderer, Size::S38, 40, 130, logic::FRAME_W - 80, 58, I18N.get(LINES[pick]));
  return true;
}

}  // namespace ugly
