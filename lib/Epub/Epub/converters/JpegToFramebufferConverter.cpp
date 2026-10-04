#include "JpegToFramebufferConverter.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <GrayThumb.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <JpegScale.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>
#include <memory>
#include <new>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "PixelCache.h"

namespace {

// Context struct passed through JPEGDEC callbacks to avoid global mutable state.
// The draw callback receives this via pDraw->pUser (set by setUserPointer()).
// The file I/O callbacks receive the HalFile* via pFile->fHandle (set by jpegOpen()).
struct JpegContext {
  GfxRenderer* renderer{nullptr};
  const RenderConfig* config{nullptr};
  int screenWidth{0};
  int screenHeight{0};

  // Source dimensions after JPEGDEC's built-in scaling
  int scaledSrcWidth{0};
  int scaledSrcHeight{0};

  // Final output dimensions
  int dstWidth{0};
  int dstHeight{0};

  // Fine scale in 16.16 fixed-point (ESP32-C3 has no FPU).
  // X and Y axes use separate scale factors: the aspect ratio of the output (dstWidth/dstHeight)
  // may differ from the source (srcWidth/srcHeight) due to integer rounding of displayHeight.
  // Using a single (X-based) scale for both axes causes the wrong srcRow to be skipped
  // during nearest-neighbor downscaling, potentially losing critical image content.
  int32_t fineScaleFPX{1 << 16};  // X: src -> dst column mapping
  int32_t invScaleFPX{1 << 16};   // X: dst -> src column mapping
  int32_t fineScaleFPY{1 << 16};  // Y: src -> dst row mapping
  int32_t invScaleFPY{1 << 16};   // Y: dst -> src row mapping

  PixelCache cache;
  bool caching{false};

  // Cover thumbnail fed the same decoded blocks (RenderConfig::thumbs), null when it declined.
  GrayThumb* thumbs{nullptr};

  // Downscale filter (see its branch in jpegDrawCallback). A destination pixel blends the two
  // source rows and columns around its center, and those can straddle a block edge: the block
  // above left its last row in carryRow (scaledSrcWidth bytes, null when it could not be had),
  // the block to the left in this MCU row its last column in carryCol and the pixel above that
  // in carryCorner.
  uint8_t* carryRow{nullptr};
  uint8_t carryCol[16]{};
  uint8_t carryCorner{0};
  int carryColBlockY{-1};

  uint32_t lastYieldMs{0};  // throttle state for yieldDuringDecode()
};

// File I/O callbacks use pFile->fHandle to access the HalFile*,
// avoiding the need for global file state.
void* jpegOpen(const char* filename, int32_t* size) {
  HalFile* f = new HalFile();
  if (!Storage.openFileForRead("JPG", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

void jpegClose(void* handle) {
  HalFile* f = reinterpret_cast<HalFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

// JPEGDEC tracks file position via pFile->iPos internally (e.g. JPEGGetMoreData
// checks iPos < iSize to decide whether more data is available). The callbacks
// MUST maintain iPos to match the actual file position, otherwise progressive
// JPEGs with large headers fail during parsing.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* pBuf, int32_t len) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return 0;
  int32_t bytesRead = f->read(pBuf, len);
  if (bytesRead < 0) return 0;
  pFile->iPos += bytesRead;
  return bytesRead;
}

int32_t jpegSeek(JPEGFILE* pFile, int32_t pos) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  if (!f->seek(pos)) return -1;
  pFile->iPos = pos;
  return pos;
}

// JPEGDEC object is ~17 KB due to internal decode buffers.
// Heap-allocate on demand so memory is only used during active decode.
constexpr size_t JPEG_DECODER_APPROX_SIZE = 20 * 1024;
constexpr size_t MIN_FREE_HEAP_FOR_JPEG = JPEG_DECODER_APPROX_SIZE + 16 * 1024;

// Fixed-point 16.16 arithmetic avoids software float emulation on ESP32-C3 (no FPU).
constexpr int FP_SHIFT = 16;
constexpr int32_t FP_ONE = 1 << FP_SHIFT;
constexpr int32_t FP_MASK = FP_ONE - 1;

// First destination index whose top (or left) source sample is at or past source index k, under
// the center-aligned mapping s = (d + 0.5) * inv - 0.5 with inv in 16.16.
int firstDstFrom(const int k, const int32_t inv) {
  if (k <= 0) return 0;
  const int64_t num = static_cast<int64_t>(2 * k + 1) * FP_ONE - inv;
  return static_cast<int>((num + 2 * static_cast<int64_t>(inv) - 1) / (2 * static_cast<int64_t>(inv)));
}

// 16.16 source position of destination index d under that mapping (never negative when inv >= 1).
inline int32_t centerSource(const int d, const int32_t inv) {
  return static_cast<int32_t>((static_cast<int64_t>(2 * d + 1) * inv - FP_ONE) >> 1);
}

int jpegDrawCallback(JPEGDRAW* pDraw) {
  JpegContext* ctx = reinterpret_cast<JpegContext*>(pDraw->pUser);
  if (!ctx || !ctx->config || !ctx->renderer) return 0;

  ImageToFramebufferDecoder::yieldDuringDecode(ctx->lastYieldMs);

  // In EIGHT_BIT_GRAYSCALE mode, pPixels contains 8-bit grayscale values
  // Buffer is densely packed: stride = pDraw->iWidth, valid columns = pDraw->iWidthUsed
  uint8_t* pixels = reinterpret_cast<uint8_t*>(pDraw->pPixels);
  const int stride = pDraw->iWidth;
  const int validW = pDraw->iWidthUsed;
  const int blockH = pDraw->iHeight;

  if (stride <= 0 || blockH <= 0 || validW <= 0) return 1;

  if (ctx->thumbs) {
    ctx->thumbs->block(pDraw->x, pDraw->y, validW, blockH, pixels, stride);
  }

  const bool useDithering = ctx->config->useDithering;
  bool caching = ctx->caching;
  const int32_t fineScaleFPX = ctx->fineScaleFPX;
  const int32_t invScaleFPX = ctx->invScaleFPX;
  const int32_t fineScaleFPY = ctx->fineScaleFPY;
  const int32_t invScaleFPY = ctx->invScaleFPY;
  GfxRenderer& renderer = *ctx->renderer;
  const int cfgX = ctx->config->x;
  const int cfgY = ctx->config->y;
  const int blockX = pDraw->x;
  const int blockY = pDraw->y;

  // Determine destination pixel range covered by this source block
  const int srcYEnd = blockY + blockH;
  const int srcXEnd = blockX + validW;

  int dstYStart = (int)((int64_t)blockY * fineScaleFPY >> FP_SHIFT);
  int dstYEnd = (srcYEnd >= ctx->scaledSrcHeight) ? ctx->dstHeight : (int)((int64_t)srcYEnd * fineScaleFPY >> FP_SHIFT);
  int dstXStart = (int)((int64_t)blockX * fineScaleFPX >> FP_SHIFT);
  int dstXEnd = (srcXEnd >= ctx->scaledSrcWidth) ? ctx->dstWidth : (int)((int64_t)srcXEnd * fineScaleFPX >> FP_SHIFT);

  // Pre-clamp destination ranges to screen bounds (eliminates per-pixel screen checks)
  int clampYMax = ctx->dstHeight;
  if (ctx->screenHeight - cfgY < clampYMax) clampYMax = ctx->screenHeight - cfgY;
  if (dstYStart < -cfgY) dstYStart = -cfgY;
  if (dstYEnd > clampYMax) dstYEnd = clampYMax;

  int clampXMax = ctx->dstWidth;
  if (ctx->screenWidth - cfgX < clampXMax) clampXMax = ctx->screenWidth - cfgX;
  if (dstXStart < -cfgX) dstXStart = -cfgX;
  if (dstXEnd > clampXMax) dstXEnd = clampXMax;

  // === Downscale (fineScale <= 1.0): bilinear around each destination pixel's center ===
  // chooseJpegScale leaves a factor in (0,5; 1], so the two nearest source rows and columns cover
  // the source cell. Destination rows and columns go to the block holding their lower source
  // sample; the upper one then sits in this block or in what the previous blocks left (carry).
  if (fineScaleFPX <= FP_ONE && fineScaleFPY <= FP_ONE && !(fineScaleFPX == FP_ONE && fineScaleFPY == FP_ONE) &&
      blockH <= 16) {
    const bool lastRow = srcYEnd >= ctx->scaledSrcHeight;
    const bool lastCol = srcXEnd >= ctx->scaledSrcWidth;
    int rowStart = firstDstFrom(blockY - 1, invScaleFPY);
    int rowEnd = lastRow ? ctx->dstHeight : firstDstFrom(srcYEnd - 1, invScaleFPY);
    int colStart = firstDstFrom(blockX - 1, invScaleFPX);
    int colEnd = lastCol ? ctx->dstWidth : firstDstFrom(srcXEnd - 1, invScaleFPX);
    if (rowStart < -cfgY) rowStart = -cfgY;
    if (rowEnd > clampYMax) rowEnd = clampYMax;
    if (colStart < -cfgX) colStart = -cfgX;
    if (colEnd > clampXMax) colEnd = clampXMax;

    const uint8_t* above = blockY > 0 && ctx->carryRow ? ctx->carryRow : nullptr;
    const bool haveLeft = blockX > 0 && ctx->carryColBlockY == blockY;
    const int lastSrcY = ctx->scaledSrcHeight - 1;
    const int lastSrcX = ctx->scaledSrcWidth - 1;
    // Row y of the source, indexed by absolute column, and the value left of this block on it.
    const auto sourceRow = [&](int y, uint8_t& left) -> const uint8_t* {
      if (y > lastSrcY) y = lastSrcY;
      if (y < blockY) {
        if (above) {
          left = haveLeft ? ctx->carryCorner : above[blockX];
          return above;
        }
        y = blockY;  // no row above: repeat the block's first row
      }
      const uint8_t* row = &pixels[(y - blockY) * stride] - blockX;
      left = haveLeft ? ctx->carryCol[y - blockY] : row[blockX];
      return row;
    };

    if (rowStart < rowEnd && colStart < colEnd) {
      DirectPixelWriter pw;
      pw.init(renderer);
      DirectCacheWriter cw;
      int cacheOriginY = 0;
      if (caching) {
        if (!ctx->cache.advanceTo(rowStart)) {
          caching = false;
          ctx->caching = false;
        } else {
          cw.init(ctx->cache.buffer, ctx->cache.bytesPerRow, ctx->cache.bandRows, ctx->cache.originX);
          cacheOriginY = ctx->config->y + ctx->cache.bandStart;
        }
      }
      for (int dstY = rowStart; dstY < rowEnd; dstY++) {
        const int outY = cfgY + dstY;
        pw.beginRow(outY);
        if (caching) cw.beginRow(outY, cacheOriginY);
        const int32_t syFP = centerSource(dstY, invScaleFPY);
        const int32_t fy = syFP & FP_MASK;
        const int y0 = syFP >> FP_SHIFT;
        uint8_t left0, left1;
        const uint8_t* row0 = sourceRow(y0, left0);
        const uint8_t* row1 = sourceRow(y0 + 1, left1);
        int32_t sxFP = centerSource(colStart, invScaleFPX);
        for (int dstX = colStart; dstX < colEnd; dstX++, sxFP += invScaleFPX) {
          const int outX = cfgX + dstX;
          const int32_t fx = sxFP & FP_MASK;
          const int x0 = sxFP >> FP_SHIFT;
          const int x1 = x0 < lastSrcX ? x0 + 1 : lastSrcX;
          const int a0 = x0 < blockX ? left0 : row0[x0];
          const int a1 = x0 < blockX ? left1 : row1[x0];
          const int top = (a0 * (FP_ONE - fx) + row0[x1] * fx) >> FP_SHIFT;
          const int bot = (a1 * (FP_ONE - fx) + row1[x1] * fx) >> FP_SHIFT;
          const uint8_t gray = static_cast<uint8_t>((top * (FP_ONE - fy) + bot * fy) >> FP_SHIFT);
          uint8_t dithered;
          if (useDithering) {
            dithered = applyBayerDither4Level(gray, outX, outY);
          } else {
            dithered = gray / 85;
            if (dithered > 3) dithered = 3;
          }
          pw.writePixel(outX, dithered);
          if (caching) cw.writePixel(outX, dithered);
        }
      }
    }

    // What the next blocks need from this one: its last column for the block to its right, and
    // its last row (and the pixel above its last column) for the MCU row below.
    for (int r = 0; r < blockH; r++) ctx->carryCol[r] = pixels[r * stride + validW - 1];
    ctx->carryColBlockY = blockY;
    if (ctx->carryRow) {
      ctx->carryCorner = ctx->carryRow[srcXEnd - 1];
      memcpy(ctx->carryRow + blockX, &pixels[(blockH - 1) * stride], validW);
    }
    return 1;
  }

  if (dstYStart >= dstYEnd || dstXStart >= dstXEnd) return 1;

  // Pre-compute orientation and render-mode state once per callback invocation
  DirectPixelWriter pw;
  pw.init(renderer);

  // The cache streams to disk one MCU-row band at a time. Flushing rows below
  // this block (raster order guarantees they are final) repositions the band;
  // cacheOriginY then maps screen rows to the band-local buffer rows. If a flush
  // write fails, stop caching for the rest of this decode (and let finalize drop
  // the partial file) rather than writing past the band buffer.
  DirectCacheWriter cw;
  int cacheOriginY = 0;
  if (caching) {
    if (!ctx->cache.advanceTo(dstYStart)) {
      caching = false;
      ctx->caching = false;
    } else {
      cw.init(ctx->cache.buffer, ctx->cache.bytesPerRow, ctx->cache.bandRows, ctx->cache.originX);
      cacheOriginY = ctx->config->y + ctx->cache.bandStart;
    }
  }

  // === 1:1 fast path: no scaling math ===
  if (fineScaleFPX == FP_ONE && fineScaleFPY == FP_ONE) {
    for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
      const int outY = cfgY + dstY;
      pw.beginRow(outY);
      if (caching) cw.beginRow(outY, cacheOriginY);
      const uint8_t* row = &pixels[(dstY - blockY) * stride];
      for (int dstX = dstXStart; dstX < dstXEnd; dstX++) {
        const int outX = cfgX + dstX;
        uint8_t gray = row[dstX - blockX];
        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }
    }
    return 1;
  }

  // === Bilinear interpolation (upscale: fineScale > 1.0) ===
  // Smooths block boundaries that would otherwise create visible banding
  // on progressive JPEG DC-only decode (1/8 resolution upscaled to target).
  if (fineScaleFPX > FP_ONE && fineScaleFPY > FP_ONE) {
    // Pre-compute safe X range where lx0 and lx0+1 are both in [0, validW-1].
    // Only the left/right edge pixels (typically 0-2 and 1-8 respectively) need clamping.
    int safeXStart = (int)(((int64_t)blockX * fineScaleFPX + FP_MASK) >> FP_SHIFT);
    int safeXEnd = (int)((int64_t)(blockX + validW - 1) * fineScaleFPX >> FP_SHIFT);
    if (safeXStart < dstXStart) safeXStart = dstXStart;
    if (safeXEnd > dstXEnd) safeXEnd = dstXEnd;
    if (safeXStart > safeXEnd) safeXEnd = safeXStart;

    for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
      const int outY = cfgY + dstY;
      pw.beginRow(outY);
      if (caching) cw.beginRow(outY, cacheOriginY);
      const int32_t srcFyFP = dstY * invScaleFPY;
      const int32_t fy = srcFyFP & FP_MASK;
      const int32_t fyInv = FP_ONE - fy;
      int ly0 = (srcFyFP >> FP_SHIFT) - blockY;
      int ly1 = ly0 + 1;
      if (ly0 < 0) ly0 = 0;
      if (ly0 >= blockH) ly0 = blockH - 1;
      if (ly1 >= blockH) ly1 = blockH - 1;

      const uint8_t* row0 = &pixels[ly0 * stride];
      const uint8_t* row1 = &pixels[ly1 * stride];

      // Left edge (with X boundary clamping)
      for (int dstX = dstXStart; dstX < safeXStart; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        int lx0 = (srcFxFP >> FP_SHIFT) - blockX;
        int lx1 = lx0 + 1;
        if (lx0 < 0) lx0 = 0;
        if (lx1 < 0) lx1 = 0;
        if (lx0 >= validW) lx0 = validW - 1;
        if (lx1 >= validW) lx1 = validW - 1;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }

      // Interior (no X boundary checks — lx0 and lx0+1 guaranteed in bounds)
      for (int dstX = safeXStart; dstX < safeXEnd; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        const int lx0 = (srcFxFP >> FP_SHIFT) - blockX;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx0 + 1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx0 + 1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }

      // Right edge (with X boundary clamping)
      for (int dstX = safeXEnd; dstX < dstXEnd; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        int lx0 = (srcFxFP >> FP_SHIFT) - blockX;
        int lx1 = lx0 + 1;
        if (lx0 >= validW) lx0 = validW - 1;
        if (lx1 >= validW) lx1 = validW - 1;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }
    }
    return 1;
  }

  // === Nearest-neighbor (one axis up, the other down) ===
  for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
    const int outY = cfgY + dstY;
    pw.beginRow(outY);
    if (caching) cw.beginRow(outY, cacheOriginY);
    const int32_t srcFyFP = dstY * invScaleFPY;
    int ly = (srcFyFP >> FP_SHIFT) - blockY;
    if (ly < 0) ly = 0;
    if (ly >= blockH) ly = blockH - 1;
    const uint8_t* row = &pixels[ly * stride];

    for (int dstX = dstXStart; dstX < dstXEnd; dstX++) {
      const int outX = cfgX + dstX;
      const int32_t srcFxFP = dstX * invScaleFPX;
      int lx = (srcFxFP >> FP_SHIFT) - blockX;
      if (lx < 0) lx = 0;
      if (lx >= validW) lx = validW - 1;
      uint8_t gray = row[lx];

      uint8_t dithered;
      if (useDithering) {
        dithered = applyBayerDither4Level(gray, outX, outY);
      } else {
        dithered = gray / 85;
        if (dithered > 3) dithered = 3;
      }
      pw.writePixel(outX, dithered);
      if (caching) cw.writePixel(outX, dithered);
    }
  }

  return 1;
}

}  // namespace

bool JpegToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_JPEG);
    return false;
  }

  std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
  if (!jpeg) {
    LOG_ERR("JPG", "Failed to allocate JPEG decoder for dimensions");
    return false;
  }

  int rc = jpeg->open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, nullptr);
  const ScopedCleanup cleanup{[&jpeg]() { jpeg->close(); }};
  if (rc != 1) {
    LOG_ERR("JPG", "Failed to open JPEG for dimensions (err=%d): %s", jpeg->getLastError(), imagePath.c_str());
    return false;
  }

  const int width = jpeg->getWidth();
  const int height = jpeg->getHeight();
  if (!validateAndStoreDimensions(width, height, out, "JPEG")) return false;
  LOG_DBG("JPG", "Image dimensions: %dx%d", width, height);

  return true;
}

bool JpegToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                     const RenderConfig& config) {
  LOG_DBG("JPG", "Decoding JPEG: %s", imagePath.c_str());

  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_JPEG);
    return false;
  }

  std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
  if (!jpeg) {
    LOG_ERR("JPG", "Failed to allocate JPEG decoder");
    return false;
  }

  JpegContext ctx;
  ctx.renderer = &renderer;
  ctx.config = &config;
  ctx.screenWidth = renderer.getScreenWidth();
  ctx.screenHeight = renderer.getScreenHeight();

  int rc = jpeg->open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDrawCallback);
  const ScopedCleanup cleanup{[&jpeg]() { jpeg->close(); }};
  if (rc != 1) {
    LOG_ERR("JPG", "Failed to open JPEG (err=%d): %s", jpeg->getLastError(), imagePath.c_str());
    return false;
  }

  ImageDimensions sourceDimensions;
  if (!validateAndStoreDimensions(jpeg->getWidth(), jpeg->getHeight(), sourceDimensions, "JPEG")) return false;
  const int srcWidth = sourceDimensions.width;
  const int srcHeight = sourceDimensions.height;

  bool isProgressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
  if (isProgressive) {
    LOG_INF("JPG", "Progressive JPEG detected - decoding DC coefficients only (lower quality)");
  }

  // Calculate overall target scale
  float targetScale;
  int destWidth, destHeight;

  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    destWidth = config.maxWidth;
    destHeight = config.maxHeight;
    targetScale = (float)destWidth / srcWidth;
  } else {
    float scaleX = (config.maxWidth > 0 && srcWidth > config.maxWidth) ? (float)config.maxWidth / srcWidth : 1.0f;
    float scaleY = (config.maxHeight > 0 && srcHeight > config.maxHeight) ? (float)config.maxHeight / srcHeight : 1.0f;
    targetScale = (scaleX < scaleY) ? scaleX : scaleY;
    if (targetScale > 1.0f) targetScale = 1.0f;

    destWidth = (int)(srcWidth * targetScale);
    destHeight = (int)(srcHeight * targetScale);
  }

  // Choose JPEGDEC built-in scaling for coarse downscaling (progressive: an eighth, see JpegScale.h).
  int jpegScaleOption;
  const int jpegScaleDenom = chooseJpegScale(targetScale, isProgressive, jpegScaleOption);

  if (destWidth <= 0 || destHeight <= 0) {
    LOG_ERR("JPG", "Degenerate output dimensions %dx%d for %s, skipping render", destWidth, destHeight,
            imagePath.c_str());
    return false;
  }

  ctx.scaledSrcWidth = (srcWidth + jpegScaleDenom - 1) / jpegScaleDenom;
  ctx.scaledSrcHeight = (srcHeight + jpegScaleDenom - 1) / jpegScaleDenom;
  ctx.dstWidth = destWidth;
  ctx.dstHeight = destHeight;
  ctx.fineScaleFPX = (int32_t)((int64_t)destWidth * FP_ONE / ctx.scaledSrcWidth);
  ctx.invScaleFPX = (int32_t)((int64_t)ctx.scaledSrcWidth * FP_ONE / destWidth);
  ctx.fineScaleFPY = (int32_t)((int64_t)destHeight * FP_ONE / ctx.scaledSrcHeight);
  ctx.invScaleFPY = (int32_t)((int64_t)ctx.scaledSrcHeight * FP_ONE / destHeight);

  LOG_DBG("JPG", "JPEG %dx%d -> %dx%d (scale %.2f, jpegScale 1/%d, fineScale %.2f)%s", srcWidth, srcHeight, destWidth,
          destHeight, targetScale, jpegScaleDenom, (float)destWidth / ctx.scaledSrcWidth,
          isProgressive ? " [progressive]" : "");

  // The downscale filter's row carried across MCU rows (one byte per decoded column); without it
  // a block's first destination rows repeat its first source row instead of blending.
  std::unique_ptr<uint8_t[]> carryRow;
  if (ctx.fineScaleFPX <= FP_ONE && ctx.fineScaleFPY <= FP_ONE) {
    carryRow.reset(new (std::nothrow) uint8_t[ctx.scaledSrcWidth]);
    ctx.carryRow = carryRow.get();
  }

  // Set pixel type to 8-bit grayscale (must be after open())
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&ctx);

  // Start streaming the pixel cache to disk. The band only needs to hold the
  // tallest single decode block: a JPEGDEC MCU cell is at most 16 scaled-source
  // rows tall, which our fine scale maps to this many output rows.
  ctx.caching = !config.cachePath.empty();
  if (ctx.caching) {
    const int maxBlockDstRows = (int)(((int64_t)16 * ctx.fineScaleFPY) >> FP_SHIFT) + 2;
    if (!ctx.cache.begin(config.cachePath, destWidth, destHeight, config.x, config.y, maxBlockDstRows)) {
      LOG_ERR("JPG", "Failed to start cache stream, continuing without caching");
      ctx.caching = false;
    }
  }

  // A JPEGDEC block is one MCU row: at most 16 source rows, fewer on the reduced grids.
  if (config.thumbs && config.thumbs->start(ctx.scaledSrcWidth, ctx.scaledSrcHeight, 16 / jpegScaleDenom)) {
    ctx.thumbs = config.thumbs;
  }
#if defined(TENOR_UI_ACCEPTANCE) || defined(TENOR_PRESS_PROBE)
  if (config.thumbs) {
    const int rows = 16 / jpegScaleDenom;
    LOG_INF("IMG", "THUMB_START grid=%dx%d rows=%d bytes=%u ok=%u free=%u", ctx.scaledSrcWidth, ctx.scaledSrcHeight,
            rows,
            static_cast<unsigned>(
                GrayThumb::bufferBytes(config.thumbs->targetHeight(), ctx.scaledSrcWidth, ctx.scaledSrcHeight, rows)),
            ctx.thumbs ? 1u : 0u, static_cast<unsigned>(ESP.getFreeHeap()));
  }
#endif

#ifdef TENOR_PRESS_PROBE
  LOG_INF("IMG", "COVER_DECODE_START free=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  unsigned long decodeStart = millis();
  ctx.lastYieldMs = decodeStart;
  rc = jpeg->decode(0, 0, jpegScaleOption);
  unsigned long decodeTime = millis() - decodeStart;
#ifdef TENOR_PRESS_PROBE
  LOG_INF("IMG", "COVER_DECODE ms=%lu ok=%u thumb=%u free=%u largest=%u", decodeTime, rc == 1 ? 1u : 0u,
          ctx.thumbs ? 1u : 0u, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif

  if (rc != 1) {
    LOG_ERR("JPG", "Decode failed (rc=%d, lastError=%d)", rc, jpeg->getLastError());
    if (ctx.caching) ctx.cache.abort();
    return false;
  }

  LOG_DBG("JPG", "JPEG decoding complete - render time: %lu ms", decodeTime);

  // Finalize the streamed cache file. Note: a flush failure mid-decode clears
  // ctx.caching (the partial file is dropped), so re-read the flag here.
  if (ctx.caching) {
    ctx.cache.finalize();
  }

  return true;
}

bool JpegToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasJpgExtension(extension);
}
