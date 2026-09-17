#include "FontInstaller.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"

namespace {

/// ASCII alphanumeric + hyphen + underscore. Deliberately not std::isalnum:
/// the check must be locale-independent and must reject every non-ASCII byte
/// (UTF-8 continuations included) rather than accept whatever the locale
/// happens to classify as alphanumeric.
constexpr bool isAllowedNameChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

constexpr char kCpfontExt[] = ".cpfont";
constexpr size_t kCpfontExtLen = sizeof(kCpfontExt) - 1;
static_assert(FontInstaller::MAX_CPFONT_FILENAME_LEN > kCpfontExtLen, "filename bound must leave room for a basename");

}  // namespace

FontInstaller::FontInstaller(SdCardFontRegistry& registry) : registry_(registry) {}

bool FontInstaller::isValidFamilyName(const char* name) {
  if (name == nullptr) return false;

  // Bounded scan: stop at MAX_FAMILY_NAME_LEN even if the input is longer, so
  // an arbitrarily long string can never be walked.
  size_t i = 0;
  for (; i < MAX_FAMILY_NAME_LEN; ++i) {
    const char c = name[i];
    if (c == '\0') break;
    if (!isAllowedNameChar(c)) return false;
  }
  return i > 0 && name[i] == '\0';
}

bool FontInstaller::isValidCpfontFilename(const char* name) {
  if (name == nullptr) return false;

  size_t nameLen = 0;
  while (nameLen < MAX_CPFONT_FILENAME_LEN && name[nameLen] != '\0') ++nameLen;
  // Either the string ended inside the bound, or it is over-long.
  if (name[nameLen] != '\0') return false;

  // Must end with ".cpfont" exactly, with a non-empty basename before it.
  if (nameLen <= kCpfontExtLen) return false;
  if (memcmp(name + nameLen - kCpfontExtLen, kCpfontExt, kCpfontExtLen) != 0) return false;

  // Basename: ASCII alphanumeric + hyphen + underscore only. No '.' at all, so
  // "Foo.cpfont.tmp" and traversal segments ("..") can never pass.
  const size_t baseLen = nameLen - kCpfontExtLen;
  for (size_t i = 0; i < baseLen; ++i) {
    if (!isAllowedNameChar(name[i])) return false;
  }
  return true;
}

bool FontInstaller::ensureFamilyDir(const char* familyName) {
  // Reject before any registry or storage access: an invalid family must not
  // create (or even probe) anything on the card.
  if (!isValidFamilyName(familyName)) {
    LOG_ERR("FONT", "Invalid family name for dir: %s", familyName ? familyName : "(null)");
    return false;
  }

  // Reuse the family's existing root if installed; otherwise pick the
  // default-write root (hidden if no roots exist yet).
  const char* root = SdCardFontRegistry::findFamilyRoot(familyName);
  if (!root) root = SdCardFontRegistry::defaultWriteRoot();
  if (!root) {
    LOG_ERR("FONT", "No fonts root available");
    return false;
  }

  char dirPath[MAX_FAMILY_DIR_PATH_SIZE];
  const int written = snprintf(dirPath, sizeof(dirPath), "%s/%s", root, familyName);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(dirPath)) {
    LOG_ERR("FONT", "Family dir path too long: %s/%s", root, familyName);
    return false;
  }

  if (!Storage.exists(root)) {
    if (!Storage.mkdir(root)) {
      LOG_ERR("FONT", "Failed to create fonts dir: %s", root);
      return false;
    }
  }

  if (!Storage.exists(dirPath)) {
    if (!Storage.mkdir(dirPath)) {
      LOG_ERR("FONT", "Failed to create family dir: %s", dirPath);
      return false;
    }
  }
  return true;
}

bool FontInstaller::validateCpfontFile(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead("FONT", path, file)) {
    LOG_ERR("FONT", "Cannot open for validation: %s", path);
    return false;
  }

  uint8_t magic[CPFONT_MAGIC_LEN];
  size_t bytesRead = file.read(magic, CPFONT_MAGIC_LEN);
  file.close();

  if (bytesRead < CPFONT_MAGIC_LEN) {
    LOG_ERR("FONT", "File too small: %s (%zu bytes)", path, bytesRead);
    return false;
  }

  if (memcmp(magic, "CPFONT\0\0", CPFONT_MAGIC_LEN) != 0) {
    LOG_ERR("FONT", "Bad magic in: %s", path);
    return false;
  }

  return true;
}

bool FontInstaller::buildFontPath(const char* family, const char* filename, char* outBuf, size_t outBufSize) {
  // Nothing writable: refuse without touching the buffer.
  if (outBuf == nullptr || outBufSize == 0) return false;
  outBuf[0] = '\0';

  // Validate before any registry lookup so invalid input has no side effects.
  if (!isValidFamilyName(family) || !isValidCpfontFilename(filename)) return false;

  // Use the same root selection as ensureFamilyDir: existing install dir wins,
  // otherwise the default-write root.
  const char* root = SdCardFontRegistry::findFamilyRoot(family);
  if (!root) root = SdCardFontRegistry::defaultWriteRoot();
  if (!root) {
    LOG_ERR("FONT", "No fonts root available for: %s", family);
    return false;
  }

  const int written = snprintf(outBuf, outBufSize, "%s/%s/%s", root, family, filename);
  if (written < 0 || static_cast<size_t>(written) >= outBufSize) {
    LOG_ERR("FONT", "Font path does not fit in %zu bytes: %s/%s/%s", outBufSize, root, family, filename);
    outBuf[0] = '\0';
    return false;
  }
  return true;
}

FontInstaller::Error FontInstaller::deleteFamily(const char* familyName) {
  if (!isValidFamilyName(familyName)) {
    return Error::INVALID_FAMILY_NAME;
  }

  // A family may exist in either root (or, edge case, both). Remove from both.
  const char* roots[] = {SdCardFontRegistry::FONTS_DIR_HIDDEN, SdCardFontRegistry::FONTS_DIR_VISIBLE};
  bool removedAny = false;
  bool sawAny = false;
  for (const char* root : roots) {
    char dirPath[MAX_FAMILY_DIR_PATH_SIZE];
    const int written = snprintf(dirPath, sizeof(dirPath), "%s/%s", root, familyName);
    // Check the complete path before probing or deleting: never operate on a
    // truncated directory name.
    if (written < 0 || static_cast<size_t>(written) >= sizeof(dirPath)) {
      LOG_ERR("FONT", "Family dir path too long: %s/%s", root, familyName);
      return Error::INVALID_FAMILY_NAME;
    }
    if (!Storage.exists(dirPath)) continue;
    sawAny = true;
    if (!Storage.removeDir(dirPath)) {
      LOG_ERR("FONT", "Failed to remove family dir: %s", dirPath);
      return Error::SD_WRITE_ERROR;
    }
    removedAny = true;
  }

  if (!sawAny) {
    LOG_DBG("FONT", "Family not found in any fonts root: %s", familyName);
    return Error::OK;  // Already gone
  }
  (void)removedAny;

  // If this was the active font, clear the setting
  if (strcmp(SETTINGS.sdFontFamilyName, familyName) == 0) {
    SETTINGS.clearSdFontFamily();
    LOG_DBG("FONT", "Cleared active SD font (deleted family: %s)", familyName);
  }

  return Error::OK;
}

void FontInstaller::refreshRegistry() { registry_.discover(); }

bool FontInstaller::isFamilyInstalled(const char* familyName) const {
  return registry_.findFamily(familyName) != nullptr;
}
