#pragma once
#include <cstdint>
#include <vector>

// The renderer as DirectPixelWriter reads it: a landscape X3-size BW frame, no strip.
class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  std::vector<uint8_t> frame = std::vector<uint8_t>(99 * 528, 0xFF);
  uint8_t* getWriteTarget() { return frame.data(); }
  int getWriteOriginY() const { return 0; }
  int getWriteRows() const { return 528; }
  RenderMode getRenderMode() const { return BW; }
  bool grayPlanesAreAbsolute() const { return false; }
  uint16_t getDisplayWidthBytes() const { return 99; }
  int getDisplayWidth() const { return 792; }
  int getDisplayHeight() const { return 528; }
  Orientation getOrientation() const { return Portrait; }
  int getScreenWidth() const { return 528; }
  int getScreenHeight() const { return 792; }
};
