#pragma once
#include <FontInstaller.h>
#include <functional>
class FontPackInstaller {
 public:
  enum class Result { OK, INVALID_PACK, INVALID_NAME, NO_SPACE, IO_ERROR, BUSY };
  static inline std::function<Result(const char*)> installCallback;
  static size_t workingSetBytes() { return 5512; }
  static Result install(const char* path) { return installCallback(path); }
  static bool isPackFilename(const char* name) {
    if (!name) return false;
    const std::string path(name);
    return path.ends_with(".cpfontpack") && FontInstaller::isValidFamilyName(path.substr(0, path.size() - 11).c_str());
  }
};
