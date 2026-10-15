#pragma once

#include <cstddef>
#include <cstdint>

class FontPackInstaller {
 public:
  enum class Result { OK, INVALID_PACK, INVALID_NAME, NO_SPACE, IO_ERROR, BUSY };
  static bool isPackFilename(const char* name);
  // Supply catalog identity only after the download has passed its size and CRC check.
  static Result install(const char* path, uint32_t verifiedSize = 0, uint32_t verifiedCrc = 0);
  static bool matchesInstalled(const char* root, const char* family, uint32_t size, uint32_t crc);
  static bool recover(const char* family);
  static unsigned scan();
  static size_t workingSetBytes();
};
