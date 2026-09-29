#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Central directory of the test book. Sizes come only from the batch lookup, which counts its
// scans, so a size written to the wrong spine item shows up in book.bin.
inline std::vector<std::pair<std::string, uint32_t>> zipEntries;
inline size_t zipScans = 0;

class ZipFile {
 public:
  struct SizeTarget {
    uint64_t hash;
    uint16_t len;
    uint16_t index;
  };

  static uint64_t fnvHash64(const char* s, const size_t len) { return std::hash<std::string_view>{}({s, len}); }
  explicit ZipFile(const std::string&) {}
  bool open() { return true; }
  bool close() { return true; }
  bool getInflatedFileSize(const char*, size_t*) { return false; }

  int fillUncompressedSizes(std::deque<SizeTarget>& targets, std::deque<uint32_t>& sizes) {
    zipScans++;
    int matched = 0;
    for (const auto& [name, size] : zipEntries) {
      const uint64_t hash = fnvHash64(name.data(), name.size());
      for (const SizeTarget& target : targets) {
        if (target.hash == hash && target.len == name.size()) {
          sizes.at(target.index) = size;
          matched++;
        }
      }
    }
    return matched;
  }
};
