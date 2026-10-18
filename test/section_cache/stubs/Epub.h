#pragma once
#include <HalStorage.h>
#include <HeapFloor.h>
#include <cstddef>
#include <map>
#include <string>
class CssParser;
class Epub {
 public:
  struct SpineItem { std::string href = "chapter.xhtml"; };
  struct TocItem { int spineIndex; std::string anchor; };
  std::string cachePath;
  std::string contents;
  int tocCount = 0;
  std::map<std::string, std::string> images;
  CssParser* cssParser = nullptr;
  // Streams still to fail: refused ones as the heap floor refuses a buffer (the count moves),
  // failed ones as a card write fails (it does not).
  mutable int refuseStreams = 0;
  mutable int failStreams = 0;
  mutable int streamCalls = 0;
  const std::string& getCachePath() const { return cachePath; }
  SpineItem getSpineItem(int) const { return {}; }
  CssParser* getCssParser() const { return cssParser; }
  int getTocIndexForSpineIndex(int) const { return tocCount > 0 ? 0 : -1; }
  int getTocItemsCount() const { return tocCount; }
  TocItem getTocItem(int index) const { return {0, "anchor-" + std::to_string(index)}; }
  std::string getLanguage() const { return "en"; }
  template <typename Output>
  bool readItemContentsToStream(const std::string& path, Output& output, size_t, bool = false) const {
    ++streamCalls;
    if (refuseStreams > 0) {
      --refuseStreams;
      heapfloor::refusals.fetch_add(1);
      return false;
    }
    if (failStreams > 0) {
      --failStreams;
      return false;
    }
    const auto image = images.find(path);
    const auto& data = image == images.end() ? contents : image->second;
    return output.write(reinterpret_cast<const uint8_t*>(data.data()), data.size()) == data.size();
  }
};
