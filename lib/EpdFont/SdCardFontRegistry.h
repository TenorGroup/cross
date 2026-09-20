#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SdCardFontFileInfo {
  uint8_t pointSize;  // parsed from filename: 14
  uint8_t style;      // always 0 in v4 (all 4 styles bundled in one file);
                      // kept for potential future formats
  uint8_t stem;       // index into SdCardFontFamilyInfo::stems
};

struct SdCardFontFamilyInfo {
  std::string name;  // directory name, e.g. "NotoSansCJK"
  // Filename part before "_<size>.cpfont" (e.g. "Bookerly-SD"), one entry for
  // nearly every family. Keeping the stem once instead of a full path per file
  // holds the registry of 30 families near 4 KB instead of 22 KB (18/09/2026).
  std::vector<std::string> stems;
  std::vector<SdCardFontFileInfo> files;
  bool hiddenRoot = true;  // "/.fonts" when true, "/fonts" otherwise

  std::string dir() const;  // "/<root>/<name>"
  // "/<root>/<name>/<stem>_<size>.cpfont", or its "weight-N" variant.
  std::string filePath(const SdCardFontFileInfo& file, uint8_t weight = 0) const;
  // Physical mask: base at bit 0, legacy outlines at 1/2, guarded variants at 3/4.
  // Resolved on demand, never at discovery. The app maps this to public levels.
  uint8_t weights(const SdCardFontFileInfo& file) const;

  const SdCardFontFileInfo* findFile(uint8_t size, uint8_t style = 0) const;
  // Installed file closest to `pointSize` (ties → smaller). nullptr when the
  // family ships nothing in `style`.
  const SdCardFontFileInfo* findNearestSize(uint8_t pointSize, uint8_t style = 0) const;
  bool hasSize(uint8_t size) const;
  std::vector<uint8_t> availableSizes() const;
};

class SdCardFontRegistry {
 public:
  static constexpr int MAX_SD_FAMILIES = 128;
  // Two top-level roots are scanned at discovery time. Hidden is preferred
  // when creating new installs; both are read from if present.
  static constexpr const char* FONTS_DIR_HIDDEN = "/.fonts";
  static constexpr const char* FONTS_DIR_VISIBLE = "/fonts";

  // Returns the existing root for `familyName` (the one that contains
  // /<root>/<familyName>/), or nullptr if the family is not installed in
  // either root. Used by writers to keep re-installs in their existing dir.
  static const char* findFamilyRoot(const char* familyName);

  // Returns the root path that should be used when creating a brand-new
  // family on disk (no prior install): the existing root if exactly one of
  // the two roots exists, otherwise the hidden root.
  static const char* defaultWriteRoot();

  // Scan SD card, populate families_. Returns true if any families found.
  bool discover();

  const std::vector<SdCardFontFamilyInfo>& getFamilies() const { return families_; }
  const SdCardFontFamilyInfo* findFamily(const std::string& name) const;
  int getFamilyIndex(const std::string& name) const;
  int getFamilyCount() const { return static_cast<int>(families_.size()); }

 private:
  std::vector<SdCardFontFamilyInfo> families_;  // sorted alphabetically

  static bool parseFilename(const char* filename, uint8_t& size, uint8_t& style, std::string& stem);
  static void scanDirectory(const char* dirPath, SdCardFontFamilyInfo& family);
  // Scan one root (e.g. "/.fonts"), append families to `out`, dedup by name.
  static void scanRoot(const char* rootPath, std::vector<SdCardFontFamilyInfo>& out);
};
