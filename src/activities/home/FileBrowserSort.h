#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace filebrowser {
enum class SortMode : uint8_t { Name, Modified, Size };
inline SortMode sortModeFromValue(int value) {
  return value >= 0 && value <= 2 ? static_cast<SortMode>(value) : SortMode::Name;
}
inline int sortModeValue(SortMode mode) { return static_cast<int>(mode); }

// Keys belong to the open dirents and live only until this in-place sort ends.
void sortNames(std::vector<std::string>& names, uint64_t* keys, SortMode mode);
}  // namespace filebrowser
