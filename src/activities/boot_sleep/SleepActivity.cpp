#include "SleepActivity.h"

#include <BitmapHelpers.h>
#include <Epub.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <PNGdec.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "QuoteStore.h"
#include "ReadingStatsStore.h"
#include "SleepQuoteLayout.h"
#include "activities/reader/ReaderUtils.h"
#include "components/ManNguTenor.h"
#include "components/QuoteMarkGlyph.h"
#include "components/ReadingStatsView.h"
#include "components/UITheme.h"
#include "components/X3BrandScreen.h"
#include "fontIds.h"
#include "images/MoonIcon.h"

namespace {

// Kept separate from /sleep.bmp and /.sleep so alpha-overlay art does not mix with full-screen wallpapers.
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr char TRANSPARENT_SLEEP_DIR[] = "/.sleep-overlay";
constexpr char TRANSPARENT_SLEEP_LEGACY_DIR[] = "/sleep-overlay";
constexpr size_t MAX_SLEEP_FILE_NAME_LEN = 256;
constexpr uint8_t MIN_VISIBLE_ALPHA = 8;

struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

struct OverlayBmpInfo {
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t dataOffset = 0;
  uint32_t rowBytes = 0;
};

uint16_t readLE16(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8);
}

uint32_t readLE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  const auto b2 = static_cast<uint8_t>(c2 < 0 ? 0 : c2);
  const auto b3 = static_cast<uint8_t>(c3 < 0 ? 0 : c3);
  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
}

uint32_t readBE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) return 0;
  return (static_cast<uint32_t>(c0) << 24) | (static_cast<uint32_t>(c1) << 16) | (static_cast<uint32_t>(c2) << 8) |
         static_cast<uint32_t>(c3);
}

bool isValidPngHeader(HalFile& file) {
  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  static constexpr uint32_t MAX_SOURCE_PIXELS = 2048u * 1536u;
  uint8_t signature[8];
  if (!file.seek(0) || file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) ||
      !std::equal(std::begin(signature), std::end(signature), std::begin(PNG_SIGNATURE))) {
    return false;
  }

  const uint32_t ihdrLength = readBE32(file);
  char chunkType[4];
  if (file.read(reinterpret_cast<uint8_t*>(chunkType), sizeof(chunkType)) != static_cast<int>(sizeof(chunkType)) ||
      ihdrLength != 13 || !std::equal(std::begin(chunkType), std::end(chunkType), "IHDR")) {
    return false;
  }

  const uint32_t width = readBE32(file);
  const uint32_t height = readBE32(file);
  const int bitDepth = file.read();
  const int colorType = file.read();
  const int compression = file.read();
  const int filter = file.read();
  const int interlace = file.read();

  const bool supportedBitDepth =
      bitDepth == 8 || ((colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_INDEXED) &&
                        (bitDepth == 1 || bitDepth == 2 || bitDepth == 4));
  const bool supportedColorType = colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_TRUECOLOR ||
                                  colorType == PNG_PIXEL_INDEXED || colorType == PNG_PIXEL_GRAY_ALPHA ||
                                  colorType == PNG_PIXEL_TRUECOLOR_ALPHA;
  return width > 0 && height > 0 && width <= 2048 && height <= 3072 && width * height <= MAX_SOURCE_PIXELS &&
         supportedBitDepth && supportedColorType && compression == 0 && filter == 0 && interlace == 0;
}

BitmapPlacement calculateBitmapPlacement(const int bitmapWidth, const int bitmapHeight, const GfxRenderer& renderer) {
  BitmapPlacement placement;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (bitmapWidth > pageWidth || bitmapHeight > pageHeight) {
    float ratio = static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - placement.cropX) * static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
      }
      placement.x = 0;
      placement.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmapWidth) / ((1.0f - placement.cropY) * static_cast<float>(bitmapHeight));
      }
      placement.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      placement.y = 0;
    }
  } else {
    placement.x = (pageWidth - bitmapWidth) / 2;
    placement.y = (pageHeight - bitmapHeight) / 2;
  }

  return placement;
}

bool parseOverlayBmpHeader(HalFile& file, OverlayBmpInfo& info, const bool logErrors) {
  if (!file) return false;
  if (!file.seek(0)) return false;

  if (readLE16(file) != 0x4D42) {
    if (logErrors) LOG_ERR("SLP", "Transparent overlay is not a BMP");
    return false;
  }

  file.seekCur(8);
  info.dataOffset = readLE32(file);

  const uint32_t dibSize = readLE32(file);
  if (dibSize < 40) {
    if (logErrors) LOG_ERR("SLP", "Unsupported BMP DIB header: %u", static_cast<unsigned>(dibSize));
    return false;
  }

  info.width = static_cast<int32_t>(readLE32(file));
  const auto rawHeight = static_cast<int32_t>(readLE32(file));
  if (rawHeight == std::numeric_limits<int32_t>::min()) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, rawHeight);
    return false;
  }
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;

  const uint16_t planes = readLE16(file);
  const uint16_t bpp = readLE16(file);
  const uint32_t compression = readLE32(file);

  // Match Bitmap::parseHeaders(): accept BI_RGB (0) and 32bpp BI_BITFIELDS (3), but keep the same
  // byte-layout assumption as custom sleep BMPs. The renderer below treats pixels as BGRA and does not parse masks.
  if (planes != 1 || bpp != 32 || !(compression == 0 || compression == 3)) {
    if (logErrors) {
      LOG_ERR("SLP", "Transparent overlay must be 32-bit BGRA BMP (planes=%u bpp=%u comp=%u)", planes, bpp,
              static_cast<unsigned>(compression));
    }
    return false;
  }

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (info.width <= 0 || info.height <= 0 || info.width > MAX_IMAGE_WIDTH || info.height > MAX_IMAGE_HEIGHT) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, info.height);
    return false;
  }

  info.rowBytes = static_cast<uint32_t>(info.width) * 4u;
  if (!file.seek(info.dataOffset)) {
    if (logErrors) LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  return true;
}

uint8_t bayerThreshold4x4(const int x, const int y) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER_4X4[((y & 0x03) << 2) | (x & 0x03)];
}

enum class TransparentOverlayPass : uint8_t { BW, GrayscaleLsb, GrayscaleMsb };

uint8_t quantizeOverlayLum(const uint8_t lum) {
  // Match Bitmap's native-palette path: 0, 85, 170, 255 map directly to levels 0..3.
  return lum >> 6;
}

bool renderTransparentOverlayPass(HalFile& file, const OverlayBmpInfo& info, const BitmapPlacement& placement,
                                  const GfxRenderer& renderer, uint8_t* row, const TransparentOverlayPass pass) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cropPixX = std::floor(info.width * placement.cropX / 2.0f);
  const int cropPixY = std::floor(info.height * placement.cropY / 2.0f);
  const float croppedWidth = (1.0f - placement.cropX) * static_cast<float>(info.width);
  const float croppedHeight = (1.0f - placement.cropY) * static_cast<float>(info.height);

  float scale = 1.0f;
  if (croppedWidth > 0.0f && croppedHeight > 0.0f) {
    const float widthScale = static_cast<float>(pageWidth) / croppedWidth;
    const float heightScale = static_cast<float>(pageHeight) / croppedHeight;
    scale = std::min(widthScale, heightScale);
    if (scale > 1.0f) scale = 1.0f;
  }
  const bool isScaled = scale < 1.0f;

  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read in transparent overlay row %d", bmpY);
      return false;
    }

    int screenY = -cropPixY + (info.topDown ? bmpY : info.height - 1 - bmpY);
    if (isScaled) screenY = std::floor(screenY * scale);
    screenY += placement.y;

    if (screenY >= pageHeight) {
      if (info.topDown) break;
      continue;
    }
    if (screenY < 0) {
      if (!info.topDown) break;
      continue;
    }

    for (int bmpX = cropPixX; bmpX < info.width - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) screenX = std::floor(screenX * scale);
      screenX += placement.x;

      if (screenX >= renderer.getScreenWidth()) break;
      if (screenX < 0) continue;

      const uint8_t* pixel = row + (static_cast<size_t>(bmpX) * 4u);
      const uint8_t alpha = pixel[3];
      if (alpha < MIN_VISIBLE_ALPHA || alpha <= bayerThreshold4x4(screenX, screenY)) continue;

      const uint8_t lum = (77u * pixel[2] + 150u * pixel[1] + 29u * pixel[0]) >> 8;
      const uint8_t level = quantizeOverlayLum(lum);

      switch (pass) {
        case TransparentOverlayPass::BW:
          // Same first pass as custom bitmap sleep: all non-white levels are painted black.
          // Transparent overlay's only difference is that opaque white explicitly erases underlying text.
          renderer.drawPixel(screenX, screenY, level < 3);
          break;
        case TransparentOverlayPass::GrayscaleLsb:
        case TransparentOverlayPass::GrayscaleMsb: {
          const auto planePixel =
              grayPlanePixel(level, pass == TransparentOverlayPass::GrayscaleMsb, renderer.grayPlanesAreAbsolute());
          if (planePixel.write) renderer.drawPixel(screenX, screenY, planePixel.black);
          break;
        }
      }
    }
  }

  return true;
}

enum class AlphaOverlayResult : uint8_t { Rendered, NotAlphaOverlay, Error };
enum class AlphaScanResult : uint8_t { Useful, NotUseful, Error };

AlphaScanResult scanForUsefulAlpha(HalFile& file, const OverlayBmpInfo& info, uint8_t* row) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return AlphaScanResult::Error;
  }

  bool hasVisiblePixel = false;
  bool hasNonOpaquePixel = false;
  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read while checking transparent overlay row %d", bmpY);
      return AlphaScanResult::Error;
    }

    for (int bmpX = 0; bmpX < info.width; bmpX++) {
      const uint8_t alpha = row[static_cast<size_t>(bmpX) * 4u + 3u];
      hasVisiblePixel |= alpha >= MIN_VISIBLE_ALPHA;
      hasNonOpaquePixel |= alpha < 255;
      if (hasVisiblePixel && hasNonOpaquePixel) return AlphaScanResult::Useful;
    }
  }

  return AlphaScanResult::NotUseful;
}

AlphaOverlayResult tryRenderTransparentOverlayBmp(HalFile& file, GfxRenderer& renderer, const char* pathForLog) {
  OverlayBmpInfo info;
  if (!parseOverlayBmpHeader(file, info, false)) return AlphaOverlayResult::NotAlphaOverlay;

  const auto placement = calculateBitmapPlacement(info.width, info.height, renderer);
  auto row = makeUniqueNoThrow<uint8_t[]>(info.rowBytes);
  if (!row) {
    LOG_ERR("SLP", "OOM: transparent overlay row (%u bytes)", static_cast<unsigned>(info.rowBytes));
    return AlphaOverlayResult::Error;
  }

  const auto alphaScanResult = scanForUsefulAlpha(file, info, row.get());
  if (alphaScanResult == AlphaScanResult::Error) return AlphaOverlayResult::Error;
  if (alphaScanResult == AlphaScanResult::NotUseful) return AlphaOverlayResult::NotAlphaOverlay;

  LOG_DBG("SLP", "Rendering transparent overlay: %s (%dx%d)", pathForLog, info.width, info.height);

  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::BW))
    return AlphaOverlayResult::Error;
  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (absolute) {
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return AlphaOverlayResult::Error;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  // Absolute planes retain B/W background bits; each visible overlay pixel is rewritten in both passes.
  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleLsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    // Keep the current display instead of trying another overlay with a
    // framebuffer that now contains an incomplete gray plane.
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleLsbBuffers();

  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleMsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return AlphaOverlayResult::Rendered;
}

enum class SleepRecentKind : uint8_t { Standard, Overlay };

bool isRecentSleepIndex(const SleepRecentKind recentKind, const uint16_t idx, const uint8_t window) {
  return recentKind == SleepRecentKind::Overlay ? APP_STATE.isRecentOverlaySleep(idx, window)
                                                : APP_STATE.isRecentSleep(idx, window);
}

void pushRecentSleepIndex(const SleepRecentKind recentKind, const uint16_t idx) {
  if (recentKind == SleepRecentKind::Overlay) {
    APP_STATE.pushRecentOverlaySleep(idx);
  } else {
    APP_STATE.pushRecentSleep(idx);
  }
}

bool findNextValidSleepImage(HalFile& dir, const SleepRecentKind recentKind, char* name) {
  for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
    if (dirFile.isDirectory()) continue;

    dirFile.getName(name, MAX_SLEEP_FILE_NAME_LEN);
    if (name[0] == '\0' || name[0] == '.') continue;

    const bool isBmp = FsHelpers::hasBmpExtension(name);
    const bool isPng = recentKind == SleepRecentKind::Overlay && FsHelpers::hasPngExtension(std::string_view{name});
    if (!isBmp && !isPng) {
      LOG_DBG("SLP", "Skipping unsupported sleep image: %s", name);
      continue;
    }

    const bool isValid = isBmp ? [&dirFile]() {
      Bitmap bitmap(dirFile);
      return bitmap.parseHeaders() == BmpReaderError::Ok;
    }()
                               : isValidPngHeader(dirFile);
    if (!isValid) {
      LOG_DBG("SLP", "Skipping invalid sleep image: %s", name);
      continue;
    }
    return true;
  }
  return false;
}

bool selectRandomSleepFile(const char* dirPath, const SleepRecentKind recentKind, std::string& selectedPath) {
  auto dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return false;

  auto name = makeUniqueNoThrow<char[]>(MAX_SLEEP_FILE_NAME_LEN);
  if (!name) {
    LOG_ERR("SLP", "OOM: sleep filename buffer");
    return false;
  }

  uint16_t fileCount = 0;
  while (fileCount < UINT16_MAX && findNextValidSleepImage(dir, recentKind, name.get())) ++fileCount;
  if (fileCount == 0) return false;

  // Pick a random wallpaper, excluding recently shown ones.
  // Window: up to SLEEP_RECENT_COUNT entries, capped at fileCount-1.
  const uint8_t recentFill =
      recentKind == SleepRecentKind::Overlay ? APP_STATE.recentOverlaySleepFill : APP_STATE.recentSleepFill;
  const uint8_t window = static_cast<uint8_t>(std::min<uint16_t>(recentFill, fileCount - 1));
  auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
  for (uint8_t attempt = 0; attempt < 20 && isRecentSleepIndex(recentKind, randomFileIndex, window); attempt++) {
    randomFileIndex = static_cast<uint16_t>(random(fileCount));
  }

  dir.rewindDirectory();
  for (uint16_t index = 0; index <= randomFileIndex; ++index) {
    if (!findNextValidSleepImage(dir, recentKind, name.get())) return false;
  }

  selectedPath.reserve(strlen(dirPath) + 1 + strlen(name.get()));
  selectedPath = dirPath;
  selectedPath += "/";
  selectedPath += name.get();
  pushRecentSleepIndex(recentKind, randomFileIndex);
  APP_STATE.saveToFile();
  return true;
}

bool drawSleepPopupPreservingFrame(GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int frameThickness = metrics.popupFrameThickness;
  const int popupY = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int popupHeight = renderer.getLineHeight(UI_12_FONT_ID) + metrics.popupMarginY * 2;
  const int bandTop = std::max(0, popupY - frameThickness);
  const int bandBottom = std::min(renderer.getScreenHeight(), popupY + popupHeight + frameThickness);
  const int bandHeight = bandBottom - bandTop;
  const size_t bandBytes = renderer.getRegionByteSize(0, bandTop, renderer.getScreenWidth(), bandHeight);

  auto savedBand = makeUniqueNoThrow<uint8_t[]>(bandBytes);
  if (!savedBand) {
    LOG_ERR("SLP", "OOM: sleep popup background (%u bytes)", static_cast<unsigned>(bandBytes));
    return false;
  }
  if (!renderer.copyRegionToBuffer(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to save sleep popup background");
    return false;
  }

  GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  if (!renderer.copyBufferToRegion(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to restore sleep popup background");
    return false;
  }
  return true;
}

void releaseSdFontCachesForDecode(const GfxRenderer& renderer) {
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("SLP", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("SLP", "Free heap before sleep image decode: %d bytes", ESP.getFreeHeap());
  }
}

// Plots the big opening mark through drawPixel, so it lands correctly whatever the byte
// alignment of its position. The bytes are packed for drawImage's blit
// (components/QuoteMarkGlyph.h), which snaps x to a whole byte on the simulator; stored
// (row, column) lands at (x + rows - 1 - row, y + column), the mapping drawIcon uses. The
// Quotes detail screen plots it the same way.
void drawQuoteMark(const GfxRenderer& renderer, const int x, const int y) {
  constexpr int stride = (QUOTE_MARK_DRAW_WIDTH + 7) / 8;
  for (int row = 0; row < QUOTE_MARK_DRAW_HEIGHT; ++row) {
    for (int column = 0; column < QUOTE_MARK_DRAW_WIDTH; ++column) {
      const uint8_t byte = kQuoteMarkGlyphBitmap[row * stride + (column >> 3)];
      if (((byte >> (7 - (column & 7))) & 1) == 0) renderer.drawPixel(x + QUOTE_MARK_DRAW_HEIGHT - 1 - row, y + column);
    }
  }
}

// Heap the cover generator needs at sleep: the JPEG decoder refuses to start below 52 KB
// free (JpegToBmpConverter.cpp), and its decoder object and row buffers are single blocks.
constexpr uint32_t COVER_MIN_FREE_HEAP = 56 * 1024;
constexpr uint32_t COVER_MIN_BLOCK = 32 * 1024;

// The sleep cover of `bookPath`, where the Cover sleep mode caches it: the variant that mode
// would pick on this panel first, then any other variant already on the card. When none is
// cached yet it is made once, now, with that mode's own generator and path, so later sleeps
// of either mode find it. The tile is left out instead, and nothing is retried in this
// sleep, when the book is gone from the card, its metadata cache is missing, the heap is
// short, or the generator fails. A generator failure is kept as "<cover>.fail" next to the
// cover it would have written, so a book whose cover image cannot be read costs one attempt
// instead of one per sleep; clearing the book's cache removes the marker with it.
std::string sleepCoverPath(const std::string& bookPath, const bool originalThresholds,
                           const CrossPointSettings& settings) {
  if (!FsHelpers::hasEpubExtension(bookPath)) return {};
  Epub epub(bookPath, "/.crosspoint");
  const bool cropped = settings.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;
  for (int variant = 0; variant < 4; variant++) {
    const std::string path = epub.getCoverBmpPath((variant & 1) ? !cropped : cropped,
                                                  (variant & 2) ? !originalThresholds : originalThresholds);
    if (Storage.exists(path.c_str())) return path;
  }
  if (!Storage.exists(bookPath.c_str())) {
    LOG_INF("SLP", "Sleep quote cover skipped: book not on card");
    return {};
  }
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t block = ESP.getMaxAllocHeap();
  if (freeHeap < COVER_MIN_FREE_HEAP || block < COVER_MIN_BLOCK) {
    LOG_INF("SLP", "Sleep quote cover skipped: heap free=%u block=%u", static_cast<unsigned>(freeHeap),
            static_cast<unsigned>(block));
    return {};
  }
  std::string path = epub.getCoverBmpPath(cropped, originalThresholds);
  const std::string failed = path.substr(0, path.size() - 4) + ".fail";
  if (Storage.exists(failed.c_str())) {
    LOG_INF("SLP", "Sleep quote cover skipped: failed before");
    return {};
  }
  const uint32_t started = millis();
  // The metadata cache the reader built is enough; building it here for a book never
  // opened would parse the whole package at sleep.
  if (!epub.load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true)) {
    LOG_INF("SLP", "Sleep quote cover skipped: no book cache");
    return {};
  }
  if (!epub.generateCoverBmp(cropped, originalThresholds)) {
    Storage.writeFile(failed.c_str(), "1");
    LOG_INF("SLP", "Sleep quote cover skipped: not made in %lu ms", static_cast<unsigned long>(millis() - started));
    return {};
  }
  HalFile made;
  const size_t bytes = Storage.openFileForRead("SLP", path, made) ? made.size() : 0;
  LOG_INF("SLP", "Sleep quote cover made=1 ms=%lu bytes=%u", static_cast<unsigned long>(millis() - started),
          static_cast<unsigned>(bytes));
  return path;
}

}  // namespace

void SleepActivity::showEnteringSleep(GfxRenderer& renderer) {
  if (drawSleepPopupPreservingFrame(renderer)) {
    LOG_INF("SLP", "Sleep transition notice shown");
  }
}

void SleepActivity::onEnter() {
  READING_STATS.markHabitsSleep();
  LOG_INF("SLP", "Timing entered-image-render at=%lu", static_cast<unsigned long>(millis()));
  Activity::onEnter();

  const bool frameWasInverted = display.isInverted();

  // Sleep screens always use normal polarity. This activity draws directly
  // from onEnter (outside ActivityManager's per-render polarity resolution),
  // so clear any inversion left over from a night-mode reader render.
  display.setInverted(false);

  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  LOG_INF("SLP", "Sleep screen mode=%u, quick=%u", SETTINGS.sleepScreen, renderQuickResume);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM) {
    // Transparent mode retains the current framebuffer. Materialize any
    // output-level inversion first so the retained content keeps its visible
    // polarity after the display driver returns to normal.
    if (frameWasInverted) renderer.invertScreen();
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    }

    if (APP_STATE.lastSleepFromReader) {
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    }
    releaseSdFontCachesForDecode(renderer);
    return renderTransparentCustomSleepScreen();
  }

  // These modes replace the whole screen. Paint only the completed sleep frame.
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    case (CrossPointSettings::SLEEP_SCREEN_MODE::STATS):
      return renderStatsSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::TENOR):
      return renderTenorSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::QUOTE):
      return renderQuoteSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

// Man ngu mac dinh cua tenor/cross: an pham branding nen thang vao firmware, nen khong ai
// phai chep file vao the nho moi co man ngu tu te.
//
// Du lieu la RLE hai byte mot doan, so diem roi toi muc, chay theo hang tu trai sang phai
// va tu tren xuong. Xem scripts/sinh_man_ngu.py.
void SleepActivity::renderTenorSleepScreen() const {
  releaseSdFontCachesForDecode(renderer);
  if (renderX3BrandScreen(renderer, false)) return;
  // X3 artwork has a fixed pixel grid. Other panels retain the text fallback.
  if (!gpio.deviceIsX3()) {
    renderDefaultSleepScreen();
    return;
  }
  renderer.clearScreen();

  static constexpr Color MUC[4] = {Color::Black, Color::DarkGray, Color::LightGray, Color::White};
  int x = 0;
  int y = 0;
  for (size_t i = 0; i + 1 < sizeof(mannogu::DU_LIEU); i += 2) {
    int con = mannogu::DU_LIEU[i];
    const uint8_t muc = mannogu::DU_LIEU[i + 1];
    while (con > 0 && y < mannogu::CAO) {
      const int trongHang = mannogu::RONG - x;
      const int ve = con < trongHang ? con : trongHang;
      // Nen da trang san sau clearScreen(), nen doan trang khong phai ve lai. Bo qua chung
      // cat phan lon so lenh ve: an pham nay 85% dien tich la nen trang.
      if (muc != 3) renderer.fillRectDither(x, y, ve, 1, MUC[muc]);
      x += ve;
      con -= ve;
      if (x >= mannogu::RONG) {
        x = 0;
        y++;
      }
    }
  }

  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

void SleepActivity::renderCustomSleepScreen() const {
  releaseSdFontCachesForDecode(renderer);
  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true,
                  renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
                      display.getController() == HalDisplay::Controller::SSD1677 &&
                      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      file.close();
      return;
    }
    file.close();
  }

  std::string selectedPath;
  if (!selectRandomSleepFile("/.sleep", SleepRecentKind::Standard, selectedPath)) {
    selectRandomSleepFile("/sleep", SleepRecentKind::Standard, selectedPath);
  }

  if (!selectedPath.empty()) {
    HalFile randFile;
    if (Storage.openFileForRead("SLP", selectedPath, randFile)) {
      LOG_DBG("SLP", "Randomly loading: %s", selectedPath.c_str());
      delay(100);
      Bitmap bitmap(randFile, true,
                    renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
                        display.getController() == HalDisplay::Controller::SSD1677 &&
                        SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderBitmapSleepScreen(bitmap);
        randFile.close();
        return;
      }
      randFile.close();
    }
  }

  renderDefaultSleepScreen();
}

// Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const bool preserveBackground) const {
  const uint32_t started = millis();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto placement = calculateBitmapPlacement(bitmap.getWidth(), bitmap.getHeight(), renderer);
  const int x = placement.x;
  const int y = placement.y;
  const float cropX = placement.cropX;
  const float cropY = placement.cropY;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  if (!preserveBackground) renderer.clearScreen();

  const bool hasGreyscale =
      bitmap.hasGreyscale() && (preserveBackground || SETTINGS.sleepScreenCoverFilter ==
                                                          CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
  const auto absoluteCaps = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute);
  const bool absolute = hasGreyscale && !preserveBackground && absoluteCaps.supported();
  const bool combined = absolute && absoluteCaps.base == HalDisplay::GrayscaleBase::Combined;
  LOG_INF("SLP", "Sleep image %dx%d, absolute=%u, combined=%u", bitmap.getWidth(), bitmap.getHeight(), absolute,
          combined);

  if (!combined && !renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY)) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  if (!preserveBackground &&
      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (absolute) {
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return;
  } else if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  LOG_INF("SLP", "Timing bitmap-base=%lu ms", static_cast<unsigned long>(millis() - started));
  if (absolute && x == 0 && y == 0 && cropX == 0 && cropY == 0) {
    if (renderer.drawBitmapAbsolutePlanes(bitmap)) {
      LOG_INF("SLP", "Timing one-pass-planes=%lu ms", static_cast<unsigned long>(millis() - started));
      renderer.displayGrayBuffer();
      renderer.setRenderMode(GfxRenderer::BW);
      LOG_INF("SLP", "Timing bitmap-visible=%lu ms", static_cast<unsigned long>(millis() - started));
      return;
    }
    if (bitmap.rewindToData() != BmpReaderError::Ok) return;
  }
  if (hasGreyscale) {
    bool ready = true;
    for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      if (bitmap.rewindToData() != BmpReaderError::Ok) {
        ready = false;
        break;
      }
      renderer.clearScreen(absolute ? 0xFF : 0x00);
      renderer.setRenderMode(plane);
      if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY)) {
        ready = false;
        break;
      }
      if (plane == GfxRenderer::GRAYSCALE_LSB)
        renderer.copyGrayscaleLsbBuffers();
      else
        renderer.copyGrayscaleMsbBuffers();
    }
    LOG_INF("SLP", "Timing bitmap-planes=%lu ms", static_cast<unsigned long>(millis() - started));
    if (ready)
      renderer.displayGrayBuffer();
    else
      LOG_ERR("SLP", "Incomplete grayscale image; keeping the current display");
    renderer.setRenderMode(GfxRenderer::BW);
  }
  LOG_INF("SLP", "Timing bitmap-visible=%lu ms", static_cast<unsigned long>(millis() - started));
}

bool SleepActivity::renderSleepOverlayFile(HalFile& file, const char* pathForLog) const {
  const auto alphaResult = tryRenderTransparentOverlayBmp(file, renderer, pathForLog);
  if (alphaResult == AlphaOverlayResult::Rendered) return true;
  if (alphaResult == AlphaOverlayResult::Error) return false;

  Bitmap bitmap(file);
  const auto parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_ERR("SLP", "Invalid sleep overlay BMP %s: %s", pathForLog, Bitmap::errorToString(parseResult));
    return false;
  }

  LOG_DBG("SLP", "Rendering regular BMP sleep overlay: %s (%dx%d)", pathForLog, bitmap.getWidth(), bitmap.getHeight());
  // drawBitmap leaves white pixels untouched; skipping the initial clear makes
  // them transparent while retaining the existing grayscale pipeline.
  renderBitmapSleepScreen(bitmap, true);
  return true;
}

bool SleepActivity::renderTransparentOverlayPng(const std::string& path) const {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(path, dimensions)) return false;

  const auto placement = calculateBitmapPlacement(dimensions.width, dimensions.height, renderer);
  RenderConfig config;
  config.x = placement.x;
  config.y = placement.y;
  config.maxWidth = renderer.getScreenWidth();
  config.maxHeight = renderer.getScreenHeight();
  config.useDithering = false;
  config.sourceCropX = placement.cropX;
  config.sourceCropY = placement.cropY;
  config.useExactDimensions = placement.cropX > 0.0f || placement.cropY > 0.0f;
  config.preserveAlpha = true;

  PngToFramebufferConverter converter;
  LOG_DBG("SLP", "Rendering transparent PNG overlay: %s (%dx%d)", path.c_str(), dimensions.width, dimensions.height);

  if (!converter.decodeToFramebuffer(path, renderer, config)) return false;
  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (absolute) {
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return false;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  // Absolute planes retain B/W background bits; each visible overlay pixel is rewritten in both passes.
  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleLsbBuffers();

  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

bool SleepActivity::renderSleepOverlayPath(const std::string& path) const {
  if (FsHelpers::hasPngExtension(path)) {
    return Storage.exists(path.c_str()) && renderTransparentOverlayPng(path);
  }

  HalFile file;
  return Storage.openFileForRead("SLP", path, file) && renderSleepOverlayFile(file, path.c_str());
}

void SleepActivity::renderTransparentCustomSleepScreen() const {
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_BMP)) return;
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_PNG)) return;

  std::string selectedPath;
  if (!selectRandomSleepFile(TRANSPARENT_SLEEP_DIR, SleepRecentKind::Overlay, selectedPath)) {
    selectRandomSleepFile(TRANSPARENT_SLEEP_LEGACY_DIR, SleepRecentKind::Overlay, selectedPath);
  }

  if (!selectedPath.empty() && renderSleepOverlayPath(selectedPath)) return;

  LOG_ERR("SLP", "No valid transparent sleep overlay found");
  renderDefaultSleepScreen();
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  // SSD absolute images use the new thresholds; other panels retain legacy tuning.
  const bool originalThresholds =
      renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
      display.getController() == HalDisplay::Controller::SSD1677 &&
      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped, originalThresholds)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped, originalThresholds);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  if (gpio.deviceIsX3()) {
    // The controller still holds the displayed page, so its differential base
    // waveform can add the moon without a full-screen flash.
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderStatsSleepScreen() const {
  releaseSdFontCachesForDecode(renderer);
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  readingstatsview::drawSleep(renderer);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderQuoteSleepScreen() const {
  using namespace sleepquote;
  // One reference each: every APP_STATE or SETTINGS use expands the store's guarded
  // static initialisation again.
  auto& state = APP_STATE;
  const auto& settings = SETTINGS;
  // Only the directory is read to choose; one record is opened, the one shown.
  std::vector<quotes::QuoteId> ids;
  quotes::listNames(0, ids);
  const auto last = std::find(ids.begin(), ids.end(), state.lastSleepQuote);
  const size_t avoid = last == ids.end() ? NO_INDEX : static_cast<size_t>(last - ids.begin());
  const size_t index = pickIndex(ids.size(), avoid, static_cast<uint32_t>(random(INT32_MAX)));
  QuoteRecord quote;
  if (index == NO_INDEX || !quotes::load(ids[index], quote)) {
    LOG_INF("SLP", "Sleep quote: none of %u readable, Tenor screen instead", static_cast<unsigned>(ids.size()));
    return renderTenorSleepScreen();
  }
  LOG_INF("SLP", "Sleep quote %s (%u of %u)", quotes::nameOf(ids[index]).c_str(), static_cast<unsigned>(index),
          static_cast<unsigned>(ids.size()));
  state.lastSleepQuote = ids[index];
  state.saveToFile();
  std::vector<quotes::QuoteId>().swap(ids);

  releaseSdFontCachesForDecode(renderer);
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const auto caps = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute);
  const bool originalThresholds = caps.supported() && display.getController() == HalDisplay::Controller::SSD1677 &&
                                  settings.sleepScreenCoverFilter ==
                                      CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  HalFile coverFile;
  const std::string coverPath = sleepCoverPath(quote.path, originalThresholds, settings);
  const bool coverOpen = !coverPath.empty() && Storage.openFileForRead("SLP", coverPath, coverFile);
  Bitmap cover(coverFile);
  const bool hasCover = coverOpen && cover.parseHeaders() == BmpReaderError::Ok && cover.getWidth() > 0 &&
                        cover.getHeight() > 0;
  // Trim the cover to the tile's own shape so it fills the tile instead of floating in it.
  float cropX = 0.0f;
  float cropY = 0.0f;
  if (hasCover) {
    const float ratio = static_cast<float>(cover.getWidth()) / static_cast<float>(cover.getHeight());
    const float tile = static_cast<float>(COVER_W) / static_cast<float>(COVER_H);
    if (ratio > tile)
      cropX = 1.0f - tile / ratio;
    else
      cropY = 1.0f - ratio / tile;
  }

  // The body at the largest size the whole quote fits at, cut at the smallest otherwise.
  // Built-in flash fonts only: nothing here is measured with a font on the card.
  static constexpr int BODY_FONTS[] = {NOTOSERIF_18_FONT_ID, NOTOSERIF_16_FONT_ID, NOTOSERIF_14_FONT_ID};
  constexpr int SIZES = sizeof(BODY_FONTS) / sizeof(BODY_FONTS[0]);
  int heights[SIZES];
  for (int i = 0; i < SIZES; i++) heights[i] = renderer.getLineHeight(BODY_FONTS[i]);
  const int bodyRight = renderer.getScreenWidth() - RIGHT_INSET;
  const int bodyWidth = bodyRight - MARGIN_X;
  const std::string text = quote.text + "\xe2\x80\x9d";
  auto* fcm = renderer.getFontCacheManager();
  std::vector<std::string> body;
  const Fit fit = chooseFit(heights, SIZES, [&](const int size, const int limit) {
    if (fcm) fcm->prewarmCache(BODY_FONTS[size], text.c_str(), 1u << EpdFontFamily::REGULAR);
    body = renderer.wrappedText(BODY_FONTS[size], text.c_str(), bodyWidth, limit + 1);
    return static_cast<int>(body.size());
  });
  const int bodyFont = BODY_FONTS[fit.size];
  if (fit.cut) {
    body = renderer.wrappedText(bodyFont, text.c_str(), bodyWidth, bodyLineLimit(heights[fit.size]));
    if (!body.empty()) {
      const auto width = [&](const std::string& line) { return renderer.getTextWidth(bodyFont, line.c_str()); };
      body.back() = closeCutLine(body.back(), bodyWidth, width);
    }
  }

  // Bottom row: title (at most three lines) and place, beside the cover or from the margin.
  const int textX = hasCover ? MARGIN_X + COVER_W + COVER_TEXT_GAP : MARGIN_X;
  constexpr int TITLE_FONT = NOTOSERIF_12_FONT_ID;
  if (fcm && !quote.title.empty()) fcm->prewarmCache(TITLE_FONT, quote.title.c_str(), 1u << EpdFontFamily::ITALIC);
  auto title = quote.title.empty() ? std::vector<std::string>{}
                                   : renderer.wrappedText(TITLE_FONT, quote.title.c_str(), bodyRight - textX,
                                                          TITLE_MAX_LINES, EpdFontFamily::ITALIC);
  if (!title.empty()) tidyEllipsis(title.back());
  char place[96];
  const std::string placeFmt = placeFormat(tr(STR_QUOTES_LIST_PLACE));
  if (!placeFmt.empty())
    snprintf(place, sizeof(place), placeFmt.c_str(), quote.spine + 1, quote.page + 1);
  else
    snprintf(place, sizeof(place), tr(STR_QUOTES_DETAIL_CHAPTER_ONLY), quote.spine + 1);
  LOG_INF("SLP", "Sleep quote size=%d lines=%u cut=%u cover=%u title=%u line=%d", fit.size,
          static_cast<unsigned>(body.size()), fit.cut, hasCover, static_cast<unsigned>(title.size()),
          heights[fit.size]);

  // One frame in the current render mode. Text and the mark come out black in every mode
  // this is called in (B/W, or an absolute gray plane, where glyphs are drawn as B/W); the
  // cover follows the mode, so each plane gets its own bits of the cover.
  const auto drawFrame = [&](const bool withCover) {
    renderer.clearScreen();
    drawQuoteMark(renderer, GLYPH_X, GLYPH_Y);
    int y = BODY_TOP;
    for (const auto& line : body) {
      renderer.drawText(bodyFont, MARGIN_X, y, line.c_str());
      y += heights[fit.size];
    }
    y = ROW_TOP + TITLE_DROP;
    for (const auto& line : title) {
      renderer.drawText(TITLE_FONT, textX, y, line.c_str(), true, EpdFontFamily::ITALIC);
      y += renderer.getLineHeight(TITLE_FONT);
    }
    if (!title.empty()) y += PLACE_GAP;
    renderer.drawText(SMALL_FONT_ID, textX, y, place);
    return !withCover || (cover.rewindToData() == BmpReaderError::Ok &&
                          renderer.drawBitmap(cover, MARGIN_X, ROW_TOP, COVER_W, COVER_H, cropX, cropY));
  };

  const uint32_t started = millis();
  const bool x3 = gpio.deviceIsX3();
  const bool gray = hasCover && cover.hasGreyscale();
  if (gray && caps.supported()) {
    // Same panel sequence as the Tenor screen (X3BrandScreen.cpp): on X3 one GC pass of the
    // B/W frame clears what the reader left on the glass, because the absolute gray pass
    // that follows has no erase phase of its own. setRenderMode(BW) would cancel an absolute
    // pass once it has begun, so inside the pass the mode only moves between the planes.
    renderer.setRenderMode(GfxRenderer::BW);
    bool ready = drawFrame(true);
    if (ready && x3) {
      renderer.displayBuffer(HalDisplay::FULL_REFRESH);
      ready = caps.base == HalDisplay::GrayscaleBase::Combined || drawFrame(true);
    }
    if (ready) ready = renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute);
    if (ready) renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    if (ready) ready = drawFrame(true);
    if (ready) renderer.copyGrayscaleLsbBuffers();
    if (ready) renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    if (ready) ready = drawFrame(true);
    if (ready) renderer.copyGrayscaleMsbBuffers();
    if (ready) renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);  // also cancels a failed partial pass
    LOG_INF("SLP", "Sleep quote gray ready=%u visible=%lu ms", ready, static_cast<unsigned long>(millis() - started));
    if (ready) return;
    // A cover that fails to read part way through: show the words alone.
    drawFrame(false);
    renderer.displayBuffer(x3 ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH);
    return;
  }

  renderer.setRenderMode(GfxRenderer::BW);
  const bool coverDrawn = drawFrame(hasCover);
  if (!coverDrawn) drawFrame(false);
  if (!gray || !coverDrawn) {
    renderer.displayBuffer(x3 ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH);
    LOG_INF("SLP", "Sleep quote bw visible=%lu ms", static_cast<unsigned long>(millis() - started));
    return;
  }
  // Panels without absolute gray: the cover's gray planes are nudges over a B/W base, the
  // same pipeline as the Cover sleep mode (renderBitmapSleepScreen).
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  bool ready = true;
  for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(plane);
    ready = cover.rewindToData() == BmpReaderError::Ok &&
            renderer.drawBitmap(cover, MARGIN_X, ROW_TOP, COVER_W, COVER_H, cropX, cropY);
    if (!ready) break;
    if (plane == GfxRenderer::GRAYSCALE_LSB)
      renderer.copyGrayscaleLsbBuffers();
    else
      renderer.copyGrayscaleMsbBuffers();
  }
  if (ready) renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  LOG_INF("SLP", "Sleep quote nudge ready=%u visible=%lu ms", ready, static_cast<unsigned long>(millis() - started));
}
