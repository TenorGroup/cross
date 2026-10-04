#pragma once
#include <cstdint>
#include <vector>
struct SdCardFontRegistry {
  struct Family {
    bool vector = false;
    std::vector<uint8_t> availableSizes() const { return {12,14,16,18,20,22,24,26}; }
  } family;
  const Family* findFamily(const char*) const { return &family; }
};
