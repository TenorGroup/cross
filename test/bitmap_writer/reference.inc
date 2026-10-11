bool GfxRenderer::reference(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight,
                             const float cropX, const float cropY, const uint8_t (*levelRows)[4],
                             const bool whiteAsTransparent) const {
  const uint32_t started = micros();
  uint32_t readUs = 0;
  if (fontCacheManager_ && fontCacheManager_->isScanning()) return false;

  float scale = 1.0f;
  bool isScaled = false;
  int cropPixX = std::floor(bitmap.getWidth() * cropX / 2.0f);
  int cropPixY = std::floor(bitmap.getHeight() * cropY / 2.0f);
  LOG_DBG("GFX", "Cropping %dx%d by %dx%d pix, is %s", bitmap.getWidth(), bitmap.getHeight(), cropPixX, cropPixY,
          bitmap.isTopDown() ? "top-down" : "bottom-up");

  const float croppedWidth = (1.0f - cropX) * static_cast<float>(bitmap.getWidth());
  const float croppedHeight = (1.0f - cropY) * static_cast<float>(bitmap.getHeight());
  bool hasTargetBounds = false;
  float fitScale = 1.0f;

  if (maxWidth > 0 && croppedWidth > 0.0f) {
    fitScale = static_cast<float>(maxWidth) / croppedWidth;
    hasTargetBounds = true;
  }

  if (maxHeight > 0 && croppedHeight > 0.0f) {
    const float heightScale = static_cast<float>(maxHeight) / croppedHeight;
    fitScale = hasTargetBounds ? std::min(fitScale, heightScale) : heightScale;
    hasTargetBounds = true;
  }

  // A crop worked out in float can leave the image a fraction of a pixel over the bounds: that is
  // not a shrink, and scaling by 0.99999 dropped a whole row.
  const bool over =
      (maxWidth > 0 && croppedWidth >= maxWidth + 1.0f) || (maxHeight > 0 && croppedHeight >= maxHeight + 1.0f);
  if (hasTargetBounds && fitScale < 1.0f && over) {
    scale = fitScale;
    isScaled = true;
  }
  LOG_DBG("GFX", "Scaling by %f - %s", scale, isScaled ? "scaled" : "not scaled");
  // A black and white 1-bit image drawn 1:1 goes to the framebuffer row by row; its white leaves
  // the page as it is, as below.
  // An image under a pixel over its box is cut at the box, not left to spill onto its neighbours.
  const int rowsW = std::min({bitmap.getWidth() - 2 * cropPixX, getScreenWidth() - x, maxWidth > 0 ? maxWidth : INT_MAX});
  const int rowsH =
      std::min({bitmap.getHeight() - 2 * cropPixY, getScreenHeight() - y, maxHeight > 0 ? maxHeight : INT_MAX});
  if (bitmap.is1Bit() && !isScaled && !levelRows && renderMode == BW && x >= 0 && y >= 0 &&
      drawBitmapRows(bitmap, x, y, cropPixX, cropPixY, rowsW, rowsH, false)) {
    preserveImagePolarity(x, y, rowsW, rowsH);
    return true;
  }
  // Other 1-bit bitmaps without a crop keep their own path.
  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f && !levelRows) {
    return drawBitmap1Bit(bitmap, x, y, maxWidth, maxHeight);
  }

  // Calculate output row size (2 bits per pixel, packed into bytes)
  // IMPORTANT: Use int, not uint8_t, to avoid overflow for images > 1020 pixels wide
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  const auto rowBytesSize = static_cast<size_t>(bitmap.getRowBytes());
  auto rowScratch = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(outputRowSize) + rowBytesSize);
  // The screen column of each source column, worked out once: the float multiply and floor per
  // pixel cost more than the SD read on the X3 (no FPU), and every row repeats them.
  auto columnTable = makeUniqueNoThrow<int16_t[]>(static_cast<size_t>(bitmap.getWidth()));
  if (!rowScratch || !columnTable) {
    LOG_ERR("GFX", "!! Failed to allocate BMP row buffers");
    return false;
  }
  auto* outputRow = rowScratch.get();
  auto* rowBytes = rowScratch.get() + outputRowSize;
  int16_t* columns = columnTable.get();
  int endX = cropPixX;  // one past the last source column that lands on the screen
  for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
    int screenX = bmpX - cropPixX;
    if (isScaled) {
      screenX = std::floor(screenX * scale);
    }
    screenX += x;  // the offset should not be scaled
    if (screenX >= getScreenWidth()) {
      break;
    }
    columns[bmpX] = static_cast<int16_t>(screenX);
    endX = bmpX + 1;
  }
  // Where every pixel is written (dithered, or an absolute gray plane), a screen pixel that later
  // source pixels land on again keeps only the last write: a cover shrunk into a small tile skips
  // most of them.
  const bool writesAll = levelRows || (renderMode != BW && absoluteGrayPlanes);
  for (int bmpX = cropPixX; writesAll && bmpX + 1 < endX; bmpX++) {
    if (columns[bmpX] == columns[bmpX + 1]) columns[bmpX] = -1;
  }
  const auto rowOf = [&](const int bmpY) {
    // The BMP's (0, 0) is the bottom-left corner (if the height is positive, top-left if negative).
    // Screen's (0, 0) is the top-left corner.
    int screenY = -cropPixY + (bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY);
    if (isScaled) {
      screenY = std::floor(screenY * scale);
    }
    return screenY + y;  // the offset should not be scaled
  };

  for (int bmpY = 0; bmpY < (bitmap.getHeight() - cropPixY); bmpY++) {
    const int screenY = rowOf(bmpY);
    if (screenY >= getScreenHeight()) {
      break;
    }

    const uint32_t readStart = micros();
    const auto rowResult = bitmap.readNextRow(outputRow, rowBytes);
    readUs += micros() - readStart;
    if (rowResult != BmpReaderError::Ok) {
      LOG_ERR("GFX", "Failed to read row %d from bitmap", bmpY);
      return false;
    }

    if (screenY < 0) {
      continue;
    }

    if (bmpY < cropPixY) {
      // Skip the row if it's outside the crop area
      continue;
    }
    if (writesAll && bmpY + 1 < bitmap.getHeight() - cropPixY && rowOf(bmpY + 1) == screenY) {
      continue;  // the next row lands on the same screen row
    }

    for (int bmpX = cropPixX; bmpX < endX; bmpX++) {
      const int screenX = columns[bmpX];
      if (screenX < 0) {
        continue;
      }

      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;
      if (whiteAsTransparent && val == 3) continue;

      if (levelRows) {
        int phyX, phyY;
        rotateCoordinates(orientation, screenX, screenY, &phyX, &phyY, panelWidth, panelHeight);
        drawPixel(screenX, screenY, !((levelRows[val][phyY & 3] >> (7 - (phyX & 7))) & 1));
      } else if (renderMode == BW && val < 3) {
        drawPixel(screenX, screenY);
      } else if (renderMode == GRAYSCALE_LSB || renderMode == GRAYSCALE_MSB) {
        const auto pixel = grayPlanePixel(val, renderMode == GRAYSCALE_MSB, absoluteGrayPlanes);
        if (pixel.write) drawPixel(screenX, screenY, pixel.black);
      }
    }
  }

  const int sourceWidth = bitmap.getWidth() - cropPixX * 2;
  const int sourceHeight = bitmap.getHeight() - cropPixY * 2;
  const int renderedWidth = isScaled ? static_cast<int>(std::floor((sourceWidth - 1) * scale)) + 1 : sourceWidth;
  const int renderedHeight = isScaled ? static_cast<int>(std::floor((sourceHeight - 1) * scale)) + 1 : sourceHeight;
  preserveImagePolarity(x, y, renderedWidth, renderedHeight);
  LOG_INF("BMP", "Timing bpp=%u mode=%u decode=%lu total=%lu ms", bitmap.getBpp(), renderMode,
          static_cast<unsigned long>(readUs / 1000), static_cast<unsigned long>((micros() - started) / 1000));
  return true;
}
