#pragma once
// One spine whose table of contents points at many anchors inside it, like a
// novel shipped as a single XHTML file.
#include <HalStorage.h>

#include <cstddef>
#include <cstdio>
#include <string>
class CssParser;
class Epub {
 public:
  struct SpineItem {
    std::string href = "mot.xhtml";
    int tocIndex = 0;
  };
  struct TocItem {
    int spineIndex;
    std::string anchor;
  };
  std::string cachePath;
  std::string contents;
  int tocCount = 0;
  mutable int tocReads = 0;
  static std::string anchorFor(int i) {
    char buf[16];
    snprintf(buf, sizeof(buf), "c%05d", i + 1);
    return buf;
  }
  const std::string& getCachePath() const { return cachePath; }
  SpineItem getSpineItem(int) const { return {}; }
  CssParser* getCssParser() const { return nullptr; }
  int getTocIndexForSpineIndex(int) const { return tocCount > 0 ? 0 : -1; }
  int getTocItemsCount() const { return tocCount; }
  TocItem getTocItem(int i) const {
    ++tocReads;
    return {0, anchorFor(i)};
  }
  std::string getLanguage() const { return "vi"; }
  template <typename Output>
  bool readItemContentsToStream(const std::string&, Output& output, size_t, bool = false) const {
    return output.write(reinterpret_cast<const uint8_t*>(contents.data()), contents.size()) == contents.size();
  }
};
