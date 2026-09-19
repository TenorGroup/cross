#pragma once
#include <HalStorage.h>
#include <cstddef>
#include <string>
class CssParser;
class Epub {
 public:
  struct SpineItem { std::string href = "chapter.xhtml"; };
  struct TocItem { int spineIndex; std::string anchor; };
  std::string cachePath;
  std::string contents;
  const std::string& getCachePath() const { return cachePath; }
  SpineItem getSpineItem(int) const { return {}; }
  CssParser* getCssParser() const { return nullptr; }
  int getTocIndexForSpineIndex(int) const { return -1; }
  int getTocItemsCount() const { return 0; }
  TocItem getTocItem(int) const { return {}; }
  std::string getLanguage() const { return "en"; }
  template <typename Output>
  bool readItemContentsToStream(const std::string&, Output& output, size_t, bool = false) const {
    return output.write(reinterpret_cast<const uint8_t*>(contents.data()), contents.size()) == contents.size();
  }
};
