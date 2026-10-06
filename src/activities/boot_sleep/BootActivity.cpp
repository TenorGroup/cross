#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "components/X3BrandScreen.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglySleep.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  // tenor/ugly on the button readers boots on its own doodle, in black and white, before the brand art.
  if (shell::uglyParts()) {
    [[maybe_unused]] const uint32_t started = millis();
    if (ugly::drawBoot(renderer)) {
      renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - 30, CROSSPOINT_VERSION);
      renderer.displayBuffer();
#ifdef UGLY_FRAME_LOG
      LOG_INF("UGLY", "boot visible=%lu ms", static_cast<unsigned long>(millis() - started));
#endif
      return;
    }
  }
  if (renderX3BrandScreen(renderer, true)) return;

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
