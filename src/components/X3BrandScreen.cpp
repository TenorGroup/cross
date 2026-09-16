#include "X3BrandScreen.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <Logging.h>

#include "X3BrandAssets.h"
#include "X3BrandCodec.h"
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
  // Separate-base panels (including the simulator) need a monochrome base.
  // X3 UC8279 defers its base and presents both absolute planes in one waveform.
  bool ready = caps.base == HalDisplay::GrayscaleBase::Combined || decode(msb, msbSize);
  if (ready) ready = renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute);
  if (ready) ready = decode(lsb, lsbSize);
  if (ready) renderer.copyGrayscaleLsbBuffers();
  if (ready) ready = decode(msb, msbSize);
  // Uploads complete synchronously before the framebuffer is reused.
  if (ready) renderer.copyGrayscaleMsbBuffers();
  if (ready) renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);  // also cancels a failed partial pass
  renderer.setOrientation(orientation);
  LOG_INF("BRAND", "%s ready=%u combined=%u visible=%lu ms", boot ? "boot" : "sleep", ready,
          caps.base == HalDisplay::GrayscaleBase::Combined, static_cast<unsigned long>(millis() - started));
  return ready;
}
