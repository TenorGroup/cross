#pragma once

#include <cstddef>

class FontPackInstaller {
 public:
  enum class Result { OK, INVALID_PACK, INVALID_NAME, NO_SPACE, IO_ERROR, BUSY };
  static bool isPackFilename(const char* name);
  static Result install(const char* path);
  static bool recover(const char* family);
  static unsigned scan();
  static size_t workingSetBytes();
};
