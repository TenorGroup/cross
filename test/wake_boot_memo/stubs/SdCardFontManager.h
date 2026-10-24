#pragma once
#include <GfxRenderer.h>
#include <SdCardFontRegistry.h>

#include <map>
#include <set>
#include <string>
#include <vector>

// Loads succeed for every file the test put on the host "card" (hostCardFiles), as the real
// manager's open would.
inline std::set<std::string> hostCardFiles;
inline std::vector<std::string> hostLoads;
// Whether the reader face the next load registers covers CJK.
inline bool hostCjk = false;

class SdCardFontManager {
 public:
  bool isBuiltinRaster() const { return false; }
  void releaseReaderForDownload(GfxRenderer&) {}
  bool loadBuiltinFamily(const SdCardFontFamilyInfo&, GfxRenderer&, uint8_t, uint8_t, int) { return false; }
  bool loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize, uint8_t weight = 0) {
    unloadAll(renderer);
    const auto* file = family.findNearestSize(pointSize);
    if (!file) return false;
    const std::string path = family.filePath(*file);
    hostLoads.push_back(path);
    if (!hostCardFiles.count(path)) return false;
    name_ = family.name;
    size_ = file->pointSize;
    weight_ = 0;
    static_cast<void>(weight);
    renderer.fonts[id_] = HostFontFace{hostCjk};
    return true;
  }
  int loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer&, uint8_t pointSize, int fontId = 0) {
    const auto* file = family.findFile(pointSize);
    if (!file) return 0;
    extra_.push_back(pointSize);
    extraIds_[pointSize] = fontId ? fontId : 100 + pointSize;
    return extraIds_[pointSize];
  }
  // A sized variant of the reader font: loaded from the card file of exactly that size.
  bool loadFamilyVariant(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize, int fontId) {
    const auto* file = family.findFile(pointSize);
    if (!file) return false;
    const std::string path = family.filePath(*file);
    hostLoads.push_back(path);
    if (!hostCardFiles.count(path)) return false;
    renderer.fonts[fontId] = HostFontFace{hostCjk};
    return true;
  }
  void unloadFont(GfxRenderer& renderer, int fontId) { renderer.fonts.erase(fontId); }
  void unloadExtraSizes(GfxRenderer& renderer) {
    renderer.fallbacks.clear();
    extra_.clear();
    extraIds_.clear();
  }
  void unloadAll(GfxRenderer& renderer) {
    renderer.fallbacks.clear();
    renderer.fonts.erase(id_);
    name_.clear();
    size_ = 0;
    extra_.clear();
    extraIds_.clear();
  }
  int getFontId(const std::string& name, uint8_t pointSize = 0) const {
    if (name_.empty() || name != name_) return 0;
    if (pointSize == 0 || pointSize == size_) return id_;
    const auto it = extraIds_.find(pointSize);
    return it == extraIds_.end() ? 0 : it->second;
  }
  const std::string& currentFamilyName() const { return name_; }
  uint8_t currentPointSize() const { return size_; }
  uint8_t currentWeight() const { return weight_; }
  const std::vector<uint8_t>& extraSizes() const { return extra_; }

 private:
  static constexpr int id_ = 42;
  std::string name_;
  uint8_t size_ = 0;
  uint8_t weight_ = 0;
  std::vector<uint8_t> extra_;
  std::map<uint8_t, int> extraIds_;
};
