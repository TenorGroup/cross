#include "X3BrandScreen.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <Logging.h>

#include "X3BrandAssets.h"
#include "X3BrandCodec.h"
#include "activities/boot_sleep/SleepGrayPlanes.h"
#include "fontIds.h"

bool renderX3BrandScreen(GfxRenderer& renderer, const bool boot) {
  const auto caps = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute);
  if (!gpio.deviceIsX3() || !caps.supported() || !renderer.hasFrameBuffer() ||
      renderer.getBufferSize() != x3brand::PLANE_BYTES)
    return false;
  const auto orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Portrait);
  if (renderer.getScreenWidth() != x3brand::WIDTH || renderer.getScreenHeight() != x3brand::HEIGHT) {
    renderer.setOrientation(orientation);
    return false;
  }
  const uint32_t started = millis();
  renderer.setRenderMode(GfxRenderer::BW);
  const uint8_t* lsb = boot ? x3brand::BOOT_LSB : x3brand::SLEEP_LSB;
  const uint8_t* msb = boot ? x3brand::BOOT_MSB : x3brand::SLEEP_MSB;
  const size_t lsbSize = boot ? sizeof(x3brand::BOOT_LSB) : sizeof(x3brand::SLEEP_LSB);
  const size_t msbSize = boot ? sizeof(x3brand::BOOT_MSB) : sizeof(x3brand::SLEEP_MSB);
  const auto decode = [&](const uint8_t* data, size_t size) {
    if (!decodeX3BrandPlane(data, size, renderer.getFrameBuffer(), renderer.getBufferSize())) return false;
    // BW glyph bits go into BOTH absolute planes, keeping the version black.
    if (boot) renderer.drawCenteredText(SMALL_FONT_ID, x3brand::HEIGHT - 30, CROSSPOINT_VERSION);
    return true;
  };
  // Sleep folds the two planes into one dithered B/W frame and shows it with a single GC pass
  // (SleepGrayPlanes.h): the glass holds it unpowered for hours, and the GC pass also erases
  // what the reader left there. Boot keeps the absolute gray waveform: it is repainted within
  // seconds, and the controller init already forces GC on the next two content paints.
  const bool fold = !boot && SleepGrayPlanes::wanted();
  SleepGrayPlanes planes(renderer, fold);
  // Ghost clear, gray sleep only. The absolute grayscale pass below is a single panel
  // activation with no erase phase, so whatever the reader left on the glass
  // shows through the art's large dark field. Drive one GC pass to the art's own
  // black and white threshold (the MSB plane) first so every pixel is driven and
  // the previous page is gone before the gray planes land.
  if (!boot && !fold && decode(msb, msbSize)) renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  // Separate-base panels (including the simulator) need a monochrome base.
  // X3 UC8279 defers its base and presents both absolute planes in one waveform.
  bool ready = fold || caps.base == HalDisplay::GrayscaleBase::Combined || decode(msb, msbSize);
  if (ready && !fold) ready = renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute);
  if (ready) ready = decode(lsb, lsbSize);
  if (ready) planes.lsb();
  if (ready) ready = decode(msb, msbSize);
  // Uploads complete synchronously before the framebuffer is reused.
  if (ready) planes.show();
  renderer.setRenderMode(GfxRenderer::BW);  // also cancels a failed partial pass
  renderer.setOrientation(orientation);
  LOG_INF("BRAND", "%s ready=%u combined=%u visible=%lu ms", boot ? "boot" : "sleep", ready,
          caps.base == HalDisplay::GrayscaleBase::Combined, static_cast<unsigned long>(millis() - started));
  return ready;
}
