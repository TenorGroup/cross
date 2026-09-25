#pragma once

#include <JPEGDEC.h>

// JPEGDEC decodes straight to a half, quarter or eighth of the source (JPEG_SCALE_*), sparing the
// inverse transform and the caller's own scaler for pixels that would only be summed away. One
// rule for every JPEG the firmware draws (book pages, covers, cover thumbnails): the largest
// reduction that still leaves the image at or above its output size, so what remains for the
// caller's scaler is a factor in (0,5; 1] and no output pixel is made up. targetScale is output
// over source on the axis that needs the most pixels. A progressive stream decodes its DC values
// only, which JPEGDEC hands over at an eighth whatever is asked; asking for that keeps its
// if/else chain from picking another scale. Returns the denominator (1, 2, 4 or 8).
inline int chooseJpegScale(const float targetScale, const bool progressive, int& jpegScaleOption) {
  if (progressive || targetScale <= 0.125f) {
    jpegScaleOption = JPEG_SCALE_EIGHTH;
    return 8;
  }
  if (targetScale <= 0.25f) {
    jpegScaleOption = JPEG_SCALE_QUARTER;
    return 4;
  }
  if (targetScale <= 0.5f) {
    jpegScaleOption = JPEG_SCALE_HALF;
    return 2;
  }
  jpegScaleOption = 0;
  return 1;
}
