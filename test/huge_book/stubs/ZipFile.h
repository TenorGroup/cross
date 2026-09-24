#pragma once
// Central directory model: entries in archive order with their inflated sizes.
// fillUncompressedSizes scans in that order and stops once every target is
// matched, like the device implementation, and counts scanned entries so the
// tests can price extra passes.
#include <HeapCapState.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

struct ZipModel {
  std::vector<std::pair<std::string, uint32_t>> entries;
  size_t scannedEntries = 0;
  size_t scans = 0;
};
inline ZipModel zipModel;

class ZipFile {
 public:
  struct SizeTarget {
    uint64_t hash;
    uint16_t len;
    uint16_t index;
  };
  static uint64_t fnvHash64(const char* s, size_t len) {
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
  int fillUncompressedSizes(std::deque<SizeTarget>& targets, std::deque<uint32_t>& sizes) {
    if (targets.empty()) return 0;
    ++zipModel.scans;
    int matched = 0;
    const int count = static_cast<int>(targets.size());
    for (const auto& [name, size] : zipModel.entries) {
      ++zipModel.scannedEntries;
      const SizeTarget key{fnvHash64(name.data(), name.size()), static_cast<uint16_t>(name.size()), 0};
      auto it = std::lower_bound(targets.begin(), targets.end(), key, [](const SizeTarget& a, const SizeTarget& b) {
        return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
      });
      for (; it != targets.end() && it->hash == key.hash && it->len == key.len; ++it) {
        if (it->index < sizes.size()) {
          sizes[it->index] = size;
          matched++;
        }
      }
      if (matched >= count) break;
    }
    return matched;
  }
  // Resumes from the entry after the previous hit and wraps, like the device.
  bool getInflatedFileSize(const char* name, size_t* size) {
    const size_t n = zipModel.entries.size();
    for (size_t step = 0; step < n; ++step) {
      const size_t i = (cursor + step) % n;
      ++zipModel.scannedEntries;
      if (zipModel.entries[i].first == name) {
        *size = zipModel.entries[i].second;
        cursor = i + 1;
        return true;
      }
    }
    return false;
  }

 private:
  size_t cursor = 0;
};
