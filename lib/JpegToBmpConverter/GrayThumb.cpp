#include "GrayThumb.h"

#include <Arduino.h>
#include <Memory.h>
#include <Print.h>

#include <algorithm>
#include <cstring>

#include "BitmapHelpers.h"

namespace {
// Heap the rest of the page render keeps after start() (the decoder and pixel cache band are
// already out of it by then).
constexpr size_t RENDER_MARGIN = 64 * 1024;

void put32(uint8_t* p, const uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}
}  // namespace

void writeBmpHeader1bit(Print& bmpOut, const int width, const int height) {
  // Rows are padded to 4 bytes; palette index 0 = black, 1 = white.
  const uint32_t imageSize = static_cast<uint32_t>((width + 31) / 32 * 4) * height;
  uint8_t h[62] = {'B', 'M'};
  put32(h + 2, 62 + imageSize);
  put32(h + 10, 62);
  put32(h + 14, 40);
  put32(h + 18, width);
  put32(h + 22, static_cast<uint32_t>(-height));  // top-down
  h[26] = 1;                                     // planes
  h[28] = 1;                                     // bits per pixel
  put32(h + 34, imageSize);
  put32(h + 38, 2835);  // 72 DPI
  put32(h + 42, 2835);
  put32(h + 46, 2);  // colors used
  put32(h + 50, 2);  // colors important
  h[58] = h[59] = h[60] = 0xFF;
  bmpOut.write(h, sizeof(h));
}

GrayThumb::GrayThumb(const int height) : height(height) {}
GrayThumb::~GrayThumb() = default;

bool GrayThumb::plan(const int srcW, const int srcH, const int blockRows) {
  if (srcW <= 0 || srcH <= 0 || blockRows <= 0) return false;
  // Same size rule as jpegFileTo1BitBmpStreamWithSize: fill height x 0.6 height, keep aspect.
  const float scaleToFitWidth = static_cast<float>(static_cast<int>(height * 0.6)) / srcW;
  const float scaleToFitHeight = static_cast<float>(height) / srcH;
  const float scale = scaleToFitWidth > scaleToFitHeight ? scaleToFitWidth : scaleToFitHeight;
  srcWidth = srcW;
  srcHeight = srcH;
  outWidth = static_cast<int>(srcW * scale);
  outHeight = static_cast<int>(srcH * scale);
  if (outWidth < 1 || outHeight < 1 || outWidth > srcW || outHeight > srcH) return false;
  scaleX = (static_cast<uint32_t>(srcW) << 16) / outWidth;
  scaleY = (static_cast<uint32_t>(srcH) << 16) / outHeight;
  // A thumbnail pixel sums its source cell in 16 bits: at most 257 source pixels of 255.
  if (((scaleX >> 16) + 1) * ((scaleY >> 16) + 1) > 257) return false;
  rowBytes = (outWidth + 7) / 8;
  // Thumbnail rows one block row can touch, plus the one it shares with the block row above.
  ringRows = static_cast<int>((static_cast<uint32_t>(blockRows - 1) << 16) / scaleY) + 2;
  return true;
}

size_t GrayThumb::bufferBytes(const int height, const int srcW, const int srcH, const int blockRows) {
  GrayThumb t(height);
  if (!t.plan(srcW, srcH, blockRows)) return 0;
  return static_cast<size_t>(t.rowBytes) * t.outHeight + static_cast<size_t>(t.ringRows) * t.outWidth * 2 +
         static_cast<size_t>(t.outWidth + 4) * 6 + sizeof(Atkinson1BitDitherer);
}

bool GrayThumb::start(const int srcW, const int srcH, const int blockRows) {
  const size_t bytes = bufferBytes(height, srcW, srcH, blockRows);
  if (bytes == 0 || !plan(srcW, srcH, blockRows) || ESP.getFreeHeap() < bytes + RENDER_MARGIN) return false;
  ring = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(ringRows) * outWidth);
  bits = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(rowBytes) * outHeight);
  ditherer = makeUniqueNoThrow<Atkinson1BitDitherer>(outWidth);
  if (!ring || !bits || !ditherer || !ditherer->isValid()) return false;
  memset(ring.get(), 0, static_cast<size_t>(ringRows) * outWidth * 2);
  memset(bits.get(), 0, static_cast<size_t>(rowBytes) * outHeight);
  return true;
}

void GrayThumb::block(const int x, const int y, const int w, const int h, const uint8_t* gray, const int stride) {
  if (failed || !ring) return;
  const int xEnd = std::min(x + w, srcWidth);
  // A new block row: every thumbnail row above its first source row is complete.
  if (x == 0) emitBelow((static_cast<uint32_t>(y) << 16) / scaleY);
  // First thumbnail column whose source span reaches x.
  const int firstOx = static_cast<int>(((static_cast<uint32_t>(x + 1) << 16) - 1) / scaleX);
  for (int r = 0; r < h && y + r < srcHeight; r++) {
    const int oy = static_cast<int>((static_cast<uint32_t>(y + r) << 16) / scaleY);
    if (oy >= outHeight) break;
    // Out of raster order, or a block taller than start() was told: the thumbnail is dropped.
    if (oy < nextEmit || oy - nextEmit >= ringRows) {
      failed = true;
      return;
    }
    uint16_t* acc = &ring[(oy % ringRows) * outWidth];
    const uint8_t* row = gray + r * stride;
    for (int ox = firstOx; ox < outWidth; ox++) {
      const int s0 = std::max(static_cast<int>((static_cast<uint32_t>(ox) * scaleX) >> 16), x);
      const int s1 = std::min(static_cast<int>((static_cast<uint32_t>(ox + 1) * scaleX) >> 16), xEnd);
      if (s0 >= xEnd) break;
      uint16_t sum = acc[ox];
      for (int s = s0; s < s1; s++) sum += row[s - x];
      acc[ox] = sum;
    }
  }
  if (xEnd == srcWidth) fedTo = std::max(fedTo, std::min(y + h, srcHeight));
}

void GrayThumb::emitBelow(const int limit) {
  for (; nextEmit < limit && nextEmit < outHeight; nextEmit++) {
    uint16_t* acc = &ring[(nextEmit % ringRows) * outWidth];
    const int y0 = static_cast<int>((static_cast<uint64_t>(nextEmit) * scaleY + 0xFFFF) >> 16);
    const int y1 = std::min(static_cast<int>((static_cast<uint64_t>(nextEmit + 1) * scaleY + 0xFFFF) >> 16), srcHeight);
    const int rows = std::max(y1 - y0, 1);
    uint8_t* dst = &bits[nextEmit * rowBytes];
    for (int ox = 0; ox < outWidth; ox++) {
      const int s0 = static_cast<int>((static_cast<uint32_t>(ox) * scaleX) >> 16);
      const int s1 = std::min(static_cast<int>((static_cast<uint32_t>(ox + 1) * scaleX) >> 16), srcWidth);
      const int count = std::max(s1 - s0, 1) * rows;
      dst[ox / 8] |= ditherer->processPixel(acc[ox] / count, ox) << (7 - (ox % 8));
      acc[ox] = 0;
    }
    ditherer->nextRow();
  }
}

bool GrayThumb::finish() {
  if (failed || !ring || fedTo < srcHeight) return false;
  emitBelow(outHeight);
  ring.reset();
  ditherer.reset();
  return nextEmit == outHeight;
}

bool GrayThumb::writeTo(Print& out) const {
  if (!bits || nextEmit != outHeight) return false;
  writeBmpHeader1bit(out, outWidth, outHeight);
  static constexpr uint8_t pad[4] = {};
  const int padBytes = (outWidth + 31) / 32 * 4 - rowBytes;
  for (int y = 0; y < outHeight; y++) {
    if (out.write(&bits[y * rowBytes], rowBytes) != static_cast<size_t>(rowBytes) ||
        out.write(pad, padBytes) != static_cast<size_t>(padBytes))
      return false;
  }
  return true;
}

bool GrayThumb::writeScaled(const int smallHeight, Print& out) const {
  if (!bits || nextEmit != outHeight) return false;
  GrayThumb small(smallHeight);
  const auto gray = makeUniqueNoThrow<uint8_t[]>(outWidth);
  if (!gray || !small.start(outWidth, outHeight, 1)) return false;
  for (int y = 0; y < outHeight; y++) {
    for (int x = 0; x < outWidth; x++) gray[x] = (bits[y * rowBytes + x / 8] >> (7 - x % 8)) & 1 ? 255 : 0;
    small.block(0, y, outWidth, 1, gray.get(), outWidth);
  }
  return small.finish() && small.writeTo(out);
}
