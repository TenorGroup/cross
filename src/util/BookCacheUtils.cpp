#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Xtc.h>

#include "ClippingStore.h"

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  constexpr char EPUB_PREFIX[] = "epub_";
  constexpr char TXT_PREFIX[] = "txt_";
  constexpr char XTC_PREFIX[] = "xtc_";

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0;
}

bool clearBookCache(const std::string& path) {
  bool cleared;
  // EPUB, TXT and Markdown share one reflowable cache (TXT and Markdown are converted to HTML and
  // laid out like an EPUB), so one cache directory covers all three.
  if (FsHelpers::hasReflowableBookExtension(path)) {
    cleared = Epub(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasXtcExtension(path)) {
    cleared = Xtc(path, "/.crosspoint").clearCache();
  } else {
    return true;
  }
  if (!cleared) LOG_ERR("BookCache", "Failed to clear cache for: %s", path.c_str());
  return cleared;
}

bool removeBookFile(const std::string& path) {
  if (!Storage.remove(path.c_str())) return false;
  const bool cleared = clearBookCache(path);
  return ClippingStore::deleteForFilePath(path, "epub") && cleared;
}
