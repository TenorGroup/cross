#pragma once

#include <SdCardFontRegistry.h>

#include <cstddef>
#include <cstdint>

/// Shared utility for font installation (device download + browser upload).
/// Handles directory creation, file validation, deletion, and registry refresh.
class FontInstaller {
 public:
  enum class Error {
    OK,
    INVALID_FAMILY_NAME,
    INVALID_FILE,
    SD_WRITE_ERROR,
    MAX_FAMILIES_REACHED,
  };

  explicit FontInstaller(SdCardFontRegistry& registry);

  /// Maximum family-name length in bytes, excluding the NUL terminator.
  /// Guarantees the name still fits CrossPointSettings::sdFontFamilyName[32].
  static constexpr size_t MAX_FAMILY_NAME_LEN = 31;

  /// Maximum .cpfont filename length in bytes, INCLUDING the ".cpfont" suffix
  /// and excluding the NUL terminator.
  static constexpr size_t MAX_CPFONT_FILENAME_LEN = 87;

  /// Largest family directory path FontInstaller builds, including the NUL:
  /// longest root "/.fonts" + '/' + a maximum-length family name.
  static constexpr size_t MAX_FAMILY_DIR_PATH_SIZE = 7 + 1 + MAX_FAMILY_NAME_LEN + 1;

  /// Largest font file path FontInstaller builds, including the NUL:
  /// "/.fonts" + '/' + family + '/' + filename. Guarantees the result fits
  /// SdCardFont::filePath_[128] and every 128-byte caller buffer.
  static constexpr size_t MAX_FONT_PATH_SIZE = 7 + 1 + MAX_FAMILY_NAME_LEN + 1 + MAX_CPFONT_FILENAME_LEN + 1;
  static_assert(MAX_FONT_PATH_SIZE == 128, "font path buffers (SdCardFont::filePath_) are 128 bytes");

  /// Validate a family name: ASCII alphanumeric + hyphen + underscore only, no
  /// path traversal, no separators. Rejects null/empty and anything longer than
  /// MAX_FAMILY_NAME_LEN bytes; the scan stops at that bound.
  static bool isValidFamilyName(const char* name);

  /// Validate a .cpfont filename: ASCII alphanumeric + hyphen + underscore
  /// basename followed by exactly ".cpfont". Rejects null/empty, path
  /// separators, traversal sequences ("../foo.cpfont" and "evil/foo.cpfont")
  /// and anything longer than MAX_CPFONT_FILENAME_LEN bytes; the scan stops at
  /// that bound.
  static bool isValidCpfontFilename(const char* name);

  /// Ensure /<root>/<family>/ exists, where <root> is /.fonts (preferred) or /fonts.
  /// Re-uses the existing root if the family is already installed; otherwise
  /// creates it under SdCardFontRegistry::defaultWriteRoot().
  bool ensureFamilyDir(const char* familyName);

  /// Validate a .cpfont file on disk (check magic bytes).
  bool validateCpfontFile(const char* path);

  /// Build the full SD path for a font file.
  /// Writes "/<root>/<family>/<filename>" to outBuf, choosing <root> the same
  /// way ensureFamilyDir does (existing install dir, else default-write root).
  /// Returns false, and leaves outBuf empty, when family or filename is invalid,
  /// when outBuf is null or too small for the complete path, or when snprintf
  /// would truncate; a partial path is never reported as success. Invalid input
  /// is rejected before any registry lookup.
  static bool buildFontPath(const char* family, const char* filename, char* outBuf, size_t outBufSize);

  /// Delete a family directory and all .cpfont files in it.
  /// If the deleted family is the active reader font, clears the setting.
  Error deleteFamily(const char* familyName);

  /// Re-run registry discovery to pick up new/removed fonts.
  void refreshRegistry();

  /// Check whether a family name already exists in the registry.
  bool isFamilyInstalled(const char* familyName) const;

 private:
  SdCardFontRegistry& registry_;

  static constexpr const char* CPFONT_MAGIC = "CPFONT\0";
  static constexpr size_t CPFONT_MAGIC_LEN = 8;
};
