#pragma once
#include <HalStorage.h>
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
  std::map<std::string, std::string> images;
  CssParser* cssParser = nullptr;
  const std::string& getCachePath() const { return cachePath; }
  SpineItem getSpineItem(int) const { return {}; }
  CssParser* getCssParser() const { return cssParser; }
  int getTocIndexForSpineIndex(int) const { return -1; }
  int getTocItemsCount() const { return 0; }
  TocItem getTocItem(int) const { return {}; }
  std::string getLanguage() const { return "en"; }
  template <typename Output>
  bool readItemContentsToStream(const std::string& path, Output& output, size_t, bool = false) const {
    const auto image = images.find(path);
    const auto& data = image == images.end() ? contents : image->second;
    return output.write(reinterpret_cast<const uint8_t*>(data.data()), data.size()) == data.size();
  }
};
