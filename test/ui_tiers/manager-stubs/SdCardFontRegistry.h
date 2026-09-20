#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct SdCardFontFileInfo { uint8_t pointSize; };
struct SdCardFontFamilyInfo {
  std::string name = "fixture";
  std::vector<SdCardFontFileInfo> files{{8},{10},{12},{14},{16},{18}};
  const SdCardFontFileInfo* findFile(uint8_t size) const {
    for (const auto& file : files) if (file.pointSize == size) return &file;
    return nullptr;
  }
  const SdCardFontFileInfo* findNearestSize(uint8_t size) const { return findFile(size); }
  uint8_t weights(const SdCardFontFileInfo&) const { return 7; }
  std::string filePath(const SdCardFontFileInfo& file, uint8_t weight) const {
    return std::to_string(1000 + file.pointSize * 10 + weight);
  }
};
