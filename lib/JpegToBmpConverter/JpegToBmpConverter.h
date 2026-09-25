#pragma once

#include <HalStorage.h>

class GrayThumb;
class Print;
class ZipFile;

class JpegToBmpConverter {
  static bool jpegFileToBmpStreamInternal(HalFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool oneBit, bool crop = true, bool originalThresholds = false);

 public:
  // Screen-sized cover; `oneBit` dithers it straight to black and white (X3 sleep, folded).
  static bool jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool crop = true, bool originalThresholds = false,
                                  bool oneBit = false);
  // Convert with custom target size (for thumbnails)
  static bool jpegFileToBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // Convert to 1-bit BMP (black and white only, no grays) for fast home screen rendering
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth,
                                              int targetMaxHeight);
  // Decodes the cover once into `thumb` and the smaller one it feeds (GrayThumb::alsoFeed), on the
  // smallest grid that still covers `thumb`. False when the thumbnail declines that grid (a cover
  // smaller than the card, a progressive stream's eighth) or the heap: the caller then decodes per
  // height with jpegFileTo1BitBmpStreamWithSize. `scale` receives the grid's denominator.
  static bool jpegFileToGrayThumb(HalFile& jpegFile, GrayThumb& thumb, int* scale);
};
