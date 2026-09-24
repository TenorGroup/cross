#pragma once

#include <cstdint>
#include <memory>

class Print;
class Atkinson1BitDitherer;

// 1-bit BMP header (top-down, black and white palette) shared by every thumbnail writer.
void writeBmpHeader1bit(Print& bmpOut, int width, int height);

// A 1-bit cover thumbnail built in RAM from gray blocks that a running decode hands over (the
// reader's cover page), so the cover is not decoded a second time for the Recent card. Same size
// rule, area averaging and dither as JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize.
// start() takes every buffer; feeding blocks allocates nothing and writes nothing, since it runs
// inside the page decode (X3: writing rows to the card from there added 2 s to that decode).
// writeTo() writes the file once the page is on the panel.
class GrayThumb {
 public:
  explicit GrayThumb(int height);
  ~GrayThumb();
  GrayThumb(const GrayThumb&) = delete;
  GrayThumb& operator=(const GrayThumb&) = delete;

  // blockRows is the tallest block the feeder hands over. False when the thumbnail would need more
  // pixels than the source has (a softer cover than the file decode gives), or when the heap
  // cannot spare the buffers and still leave the page render its margin.
  bool start(int srcWidth, int srcHeight, int blockRows);
  // Gray pixels of the source block at (x, y), rows `stride` bytes apart. Blocks of one block row
  // arrive left to right, block rows top to bottom (JPEGDEC's raster MCU order).
  void block(int x, int y, int w, int h, const uint8_t* gray, int stride);
  // Closes the last rows. True when every source row came in and every thumbnail row is set.
  bool finish();
  bool writeTo(Print& out) const;
  // The theme's smaller thumbnail, scaled from this one and written straight to out.
  bool writeScaled(int height, Print& out) const;

  int width() const { return outWidth; }
  // Bytes start() takes for a source of this size, for the heap check and for the tests.
  static size_t bufferBytes(int height, int srcWidth, int srcHeight, int blockRows);

 private:
  bool plan(int srcWidth, int srcHeight, int blockRows);
  void emitBelow(int limit);
  int height;
  int srcWidth = 0, srcHeight = 0, outWidth = 0, outHeight = 0, rowBytes = 0, ringRows = 0;
  int nextEmit = 0, fedTo = 0;
  uint32_t scaleX = 0, scaleY = 0;
  bool failed = false;
  std::unique_ptr<uint16_t[]> ring;  // sums of the thumbnail rows still open, ringRows x outWidth
  std::unique_ptr<uint8_t[]> bits;   // finished rows, 1 bit per pixel, no BMP padding
  std::unique_ptr<Atkinson1BitDitherer> ditherer;
};
