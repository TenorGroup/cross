#pragma once

#include <Bitmap.h>
#include <Epub/converters/PixelCache.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdint>

// The sleep screen of a JPEG: the 2-bit pixels the JPEG converter streamed to a .pxc file (PixelCache.h), written
// as the 2-bit top-down BMP the sleep screen reads.
namespace pxcsleep {
constexpr uint8_t GRAYSCALE_PALETTE[] = {0, 0, 0, 0, 85, 85, 85, 0, 170, 170, 170, 0, 255, 255, 255, 0};

// False when the .pxc is not an image of width x height, is cut short, or a read or write fails.
inline bool toBmp(const char* pxcPath, const char* bmpPath, const int expectedWidth, const int expectedHeight) {
  HalFile input, output;
  if (!Storage.openFileForRead("BMP", pxcPath, input)) return false;
  uint8_t head[pxcfile::HEADER_BYTES];
  if (input.read(head, sizeof(head)) != static_cast<int>(sizeof(head)) || !pxcfile::headerIsCurrent(head)) {
    LOG_ERR("BMP", "Not a current sleep image pixel file");
    return false;
  }
  const uint16_t width = static_cast<uint16_t>(head[4] | head[5] << 8);
  const uint16_t height = static_cast<uint16_t>(head[6] | head[7] << 8);
  if (width != expectedWidth || height != expectedHeight) {
    LOG_ERR("BMP", "Invalid sleep image pixels");
    return false;
  }
  const size_t pixelRowBytes = (width + 3) / 4;
  const size_t bmpRowBytes = (width + 15) / 16 * 4;
  if (input.size() != pxcfile::HEADER_BYTES + pixelRowBytes * height) {
    LOG_ERR("BMP", "Incomplete sleep image pixels");
    return false;
  }
  auto row = makeUniqueNoThrow<uint8_t[]>(bmpRowBytes);
  if (!row) {
    LOG_ERR("BMP", "OOM: sleep image row");
    return false;
  }
  if (!Storage.openFileForWrite("BMP", bmpPath, output)) return false;
  BmpHeader header;
  createBmpHeader(&header, width, height, BmpRowOrder::TopDown);
  header.infoHeader.biBitCount = 2;
  header.infoHeader.biClrUsed = header.infoHeader.biClrImportant = 4;
  header.infoHeader.biSizeImage = bmpRowBytes * height;
  header.fileHeader.bfOffBits = sizeof(header) - sizeof(header.colors) + sizeof(GRAYSCALE_PALETTE);
  header.fileHeader.bfSize = header.fileHeader.bfOffBits + header.infoHeader.biSizeImage;
  if (output.write(&header, sizeof(header) - sizeof(header.colors)) != sizeof(header) - sizeof(header.colors) ||
      output.write(GRAYSCALE_PALETTE, sizeof(GRAYSCALE_PALETTE)) != sizeof(GRAYSCALE_PALETTE)) {
    LOG_ERR("BMP", "Failed to write sleep image header");
    return false;
  }
  for (int y = 0; y < height; ++y) {
    if (input.read(row.get(), pixelRowBytes) != pixelRowBytes || output.write(row.get(), bmpRowBytes) != bmpRowBytes) {
      LOG_ERR("BMP", "Failed to write sleep image row %d", y);
      return false;
    }
  }
  if (!output.close()) {
    LOG_ERR("BMP", "Failed to close sleep image");
    return false;
  }
  return true;
}
}  // namespace pxcsleep
