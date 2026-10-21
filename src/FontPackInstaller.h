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
  // The family folder came from a pack (its pack.json or receipt is there), not from the user's own files.
  static bool isPackFamily(const char* root, const char* family);
  // The line spacing factor the pack was built for ("line_factor" of the first entry of its pack.json: all entries share it; the stacked capitals of
  // the family clear the line above at that factor). 0 when the family has no pack.json or no factor in it.
  static float lineFactor(const char* root, const char* family);
  static bool recover(const char* family);
  static unsigned scan();
  static size_t workingSetBytes();
};
