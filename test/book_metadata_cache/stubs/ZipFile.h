#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>

// Inflated sizes of the archive's entries, by path.
inline std::map<std::string, uint32_t> zipEntrySizes;

class ZipFile {
 public:
  struct SizeTarget {
    uint64_t hash;
    uint16_t len;
    uint16_t index;
  };

  static uint64_t fnvHash64(const char* s, const size_t len) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
      hash ^= static_cast<uint8_t>(s[i]);
      hash *= 1099511628211ull;
    }
    return hash;
  }

  explicit ZipFile(const std::string&) {}
  bool open() { return true; }
  bool close() { return true; }

  int fillUncompressedSizes(const std::deque<SizeTarget>& targets, std::deque<uint32_t>& sizes) const {
    int matched = 0;
    for (const auto& [path, size] : zipEntrySizes) {
      const uint64_t hash = fnvHash64(path.data(), path.size());
      for (const auto& target : targets) {
        if (target.hash == hash && target.len == path.size() && target.index < sizes.size()) {
          sizes[target.index] = size;
          matched++;
        }
      }
    }
    return matched;
  }

  bool getInflatedFileSize(const char* path, size_t* size) const {
    const auto it = zipEntrySizes.find(path);
    if (it == zipEntrySizes.end()) return false;
    *size = it->second;
    return true;
  }
};
