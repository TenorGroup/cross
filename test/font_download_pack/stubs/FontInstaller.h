#pragma once
#include <cctype>
#include <cstdio>
#include <cstring>
#include <HalStorage.h>
class FontInstaller {
 public:
  static constexpr size_t MAX_FONT_PATH_SIZE = 128;
  static bool isValidFamilyName(const char* name) {
    if (!name || !*name || std::strlen(name) > 31) return false;
    for (const char* cursor = name; *cursor; ++cursor)
      if (!std::isalnum(static_cast<unsigned char>(*cursor)) && *cursor != '_' && *cursor != '-') return false;
    return true;
  }
  static bool isValidCpfontRelativePath(const char* name) {
    if (!name) return false;
    std::string path(name);
    if (path.starts_with("weight-")) {
      if (path.size() < 10 || path[7] < '1' || path[7] > '6' || path[8] != '/') return false;
      path.erase(0, 9);
    }
    return path.ends_with(".cpfont") && isValidFamilyName(path.substr(0, path.size() - 7).c_str());
  }
  static bool buildFontPath(const char* family, const char* name, char* path, size_t size) {
    if (!isValidFamilyName(family) || !isValidCpfontRelativePath(name)) return false;
    return std::snprintf(path, size, "/fonts/%s/%s", family, name) < static_cast<int>(size);
  }
  bool ensureFamilyDir(const char* family) {
    const auto path = std::string("/fonts/") + family;
    return Storage.exists(path.c_str()) || Storage.mkdir(path.c_str());
  }
  bool ensureFontDir(const char* family, const char*) { return ensureFamilyDir(family); }
  bool validateCpfontFile(const char* path) {
    HalFile file;
    char magic[8]{};
    return Storage.openFileForRead("TEST", path, file) && file.read(magic, sizeof(magic)) == 8 &&
           std::memcmp(magic, "CPFONT\0\0", 8) == 0;
  }
  bool deleteFamily(const char* family) { return Storage.removeDir((std::string("/fonts/") + family).c_str()); }
};
