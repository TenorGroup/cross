#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "VectorFontSupport.h"

struct SdCardFontFileInfo {
  uint8_t pointSize;  // parsed from filename: 14 (0 for size-free vector fonts)
  uint8_t style;      // .cpfont: always 0 (all 4 styles bundled in one file).
                      // Vector family in a folder: the style ROLE of this file:
                      // 0=regular, 1=bold, 2=italic, 3=bold-italic.
  uint8_t stem;       // index into SdCardFontFamilyInfo::stems (.cpfont only)
#if CROSSPOINT_VECTOR_FONTS
  std::string path;   // full path of a vector (.ttf/.otf/.ttc) file
#endif
};

struct SdCardFontFamilyInfo {
  // Buffer size for filePath(), matching the installer's own path buffers.
  // Discovery drops files whose path would not fit, so every registered file
  // can be opened through filePath() with a buffer of this size.
  static constexpr size_t PATH_BUFFER_SIZE = 160;

  std::string name;  // directory name, e.g. "NotoSansCJK"
  // Filename part before "_<size>.cpfont" (e.g. "Bookerly-SD"), one entry for
  // nearly every family. Keeping the stem once instead of a full path per file
  // holds the registry of 30 families near 4 KB instead of 22 KB (18/09/2026).
  std::vector<std::string> stems;
  std::vector<SdCardFontFileInfo> files;
  bool hiddenRoot = true;  // "/.fonts" when true, "/fonts" otherwise
  // true for TrueType/OpenType files (.ttf/.otf/.ttc) rendered at any size via
  // the FreeInkFont engine (PSRAM boards only, see VectorFontSupport.h): `files`
  // holds one entry per style role, each with its full path in `path` and
  // pointSize 0 (size-free). false = a directory of pre-rasterized .cpfont files.
  bool vector = false;

  std::string dir() const;  // "/<root>/<name>"
  // "/<root>/<name>/<stem>_<size>.cpfont", or its "weight-N" variant. A vector family answers with the file's own
  // `path` (no weight variants). Empty when the file does not belong to this family.
  std::string filePath(const SdCardFontFileInfo& file, uint8_t weight = 0) const;
  // The same base path (weight 0) written into a caller buffer of PATH_BUFFER_SIZE bytes, for loaders that hold
  // no heap string. Returns false when `outSize` is too small or the file has no path; `out` is then truncated.
  bool filePath(const SdCardFontFileInfo& file, char* out, size_t outSize) const;
  // Highest weight directory a family can have: weight-2 to weight-6 hold the heavier variants, weight-7 to
  // weight-9 the thinner ones (readerInk physical numbers).
  static constexpr uint8_t MAX_WEIGHT = 9;
  // Physical mask of the base file and weight-1 to weight-6, base at bit 0 (the heavier variants).
  // Resolved on demand, never at discovery. The app maps this to public levels.
  uint8_t weights(const SdCardFontFileInfo& file) const;
  // One weight directory (any of 1..MAX_WEIGHT) holds this size's file. The thin variants are looked up
  // singly, so the common case pays no extra card reads.
  bool hasWeight(const SdCardFontFileInfo& file, uint8_t weight) const;

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
  static constexpr size_t MAX_FAMILY_NAME_BYTES = 63;
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

  // Scan SD card, populate families_. Invalidates pointers and references
  // obtained from getFamilies()/findFamily(). Returns true if any families found.
  bool discover();

  const std::vector<SdCardFontFamilyInfo>& getFamilies() const { return families_; }
  // Takes a catalog an earlier discover() read (kept across deep sleep) in place of a walk.
  void adopt(std::vector<SdCardFontFamilyInfo> families) { families_ = std::move(families); }
  const SdCardFontFamilyInfo* findFamily(const std::string& name) const;
  int getFamilyIndex(const std::string& name) const;
  int getFamilyCount() const { return static_cast<int>(families_.size()); }

#if CROSSPOINT_VECTOR_FONTS
  // FtFont::ReadFn over a HalFile* ctx (absolute-offset reads; count 0 is a
  // seek probe). Shared by face inspection here and streamed TTF sources
  // (SdCardFontSystem).
  static unsigned long halFileRead(void* ctx, unsigned long offset, unsigned char* buffer, unsigned long count);
#endif

 private:
  std::vector<SdCardFontFamilyInfo> families_;  // sorted alphabetically

  static bool parseFilename(const char* filename, uint8_t& size, uint8_t& style, std::string& stem);
#if CROSSPOINT_VECTOR_FONTS
  // Match a loose vector font filename (.ttf/.otf/.ttc, case-insensitive) and
  // return the length of the base name (extension stripped) in `baseLen`.
  static bool parseVectorFontName(const char* filename, size_t& baseLen);
  // Style role (0=regular, 1=bold, 2=italic, 3=bold-italic) inferred from a
  // vector font's base name (case-insensitive "bold"/"italic"/"oblique" tokens).
  static uint8_t parseVectorStyle(const char* baseName, size_t baseLen);
  // Refine each vector file's style role from its real face metadata
  // (FtFont::inspectStream: OS/2 weight + italic flag), keeping the
  // filename-derived role when the face can't be read. Then dedup by role.
  static void refineVectorStyles(const char* dirPath, std::vector<SdCardFontFileInfo>& files);
#endif
  static void scanDirectory(const char* dirPath, SdCardFontFamilyInfo& family);
  // Scan one root (e.g. "/.fonts"), append families to `out`, dedup by name.
  static void scanRoot(const char* rootPath, std::vector<SdCardFontFamilyInfo>& out);
};
