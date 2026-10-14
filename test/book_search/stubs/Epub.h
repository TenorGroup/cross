#pragma once
#include <Print.h>
#include <algorithm>
#include <string>
#include <vector>

class Epub {
 public:
  std::string cache;
  std::vector<std::string> chapters;
  mutable size_t reads = 0;
  mutable size_t chunk = 0;
  bool failRead = false;
  struct Spine { std::string href; };
  const std::string& getCachePath() const { return cache; }
  int getSpineItemsCount() const { return static_cast<int>(chapters.size()); }
  Spine getSpineItem(int index) const { return {std::to_string(index)}; }
  bool readItemContentsToStream(const std::string& path, Print& sink, size_t size, bool early) const {
    ++reads;
    chunk = size;
    if (failRead) return false;
    const auto& html = chapters[std::stoul(path)];
    for (size_t at = 0; at < html.size(); at += size) {
      const size_t count = std::min(size, html.size() - at);
      if (sink.write(reinterpret_cast<const uint8_t*>(html.data() + at), count) != count) return early;
    }
    return true;
  }
};
