#include "FileActions.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <cstring>
#include <utility>
#include <vector>

#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "util/BookCacheUtils.h"
#include "util/BookmarkUtil.h"

std::string getFileExtension(const std::string& filename);

namespace fileactions {
namespace {
constexpr size_t NAME_BUFFER_SIZE = 500;

std::string getBookCachePath(const std::string& path) {
  const char* prefix = nullptr;
  if (FsHelpers::hasEpubExtension(path)) {
    prefix = "epub_";
  } else if (FsHelpers::hasXtcExtension(path)) {
    prefix = "xtc_";
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    prefix = "txt_";
  } else {
    return "";
  }
  return std::string("/.crosspoint/") + prefix + std::to_string(std::hash<std::string>{}(path));
}

bool moveStatePath(const std::string& oldPath, const std::string& newPath, bool& moved) {
  moved = false;
  if (oldPath.empty() || !Storage.exists(oldPath.c_str())) return true;
  if (Storage.exists(newPath.c_str())) {
    LOG_ERR("FileBrowser", "Rename state target already exists: %s", newPath.c_str());
    return false;
  }
  moved = Storage.rename(oldPath.c_str(), newPath.c_str());
  if (!moved) LOG_ERR("FileBrowser", "Failed to move rename state: %s -> %s", oldPath.c_str(), newPath.c_str());
  return moved;
}

void rollBackStatePath(const std::string& oldPath, const std::string& newPath, const bool moved) {
  if (moved && !Storage.rename(newPath.c_str(), oldPath.c_str())) {
    LOG_ERR("FileBrowser", "Failed to roll back rename state: %s -> %s", newPath.c_str(), oldPath.c_str());
  }
}
// The new path of the file once renamed to `newStem` + `extension`, or empty when it stays where it was.
std::string moveFile(const std::string& oldPath, const std::string& oldEntry, const std::string& newStem,
                     const std::string& extension) {
  const std::string newEntry = newStem + extension;
  if (newStem.empty() || newEntry.size() >= NAME_BUFFER_SIZE || !FsHelpers::isSafePathComponent(newEntry)) {
    LOG_ERR("FileBrowser", "Invalid rename target: %s", newEntry.c_str());
    return {};
  }
  if (newEntry == utf8ComposeNfc(oldEntry)) return {};

  const std::string parentPath = FsHelpers::extractFolderPath(oldPath);
  const std::string newPath = (parentPath == "/" ? parentPath : parentPath + "/") + newEntry;
  if (Storage.exists(newPath.c_str())) {
    LOG_ERR("FileBrowser", "Rename target already exists: %s", newPath.c_str());
    return {};
  }

  const std::string oldCachePath = getBookCachePath(oldPath);
  const std::string newCachePath = getBookCachePath(newPath);
  const bool isEpub = FsHelpers::hasEpubExtension(oldPath);
  const std::string oldBookmarkPath = isEpub ? BookmarkUtil::getBookmarkPath(oldPath) : "";
  const std::string newBookmarkPath = isEpub ? BookmarkUtil::getBookmarkPath(newPath) : "";
  bool cacheMoved = false;
  bool bookmarksMoved = false;
  if (!moveStatePath(oldCachePath, newCachePath, cacheMoved)) return {};
  if (!moveStatePath(oldBookmarkPath, newBookmarkPath, bookmarksMoved)) {
    rollBackStatePath(oldCachePath, newCachePath, cacheMoved);
    return {};
  }
  if (!Storage.rename(oldPath.c_str(), newPath.c_str())) {
    LOG_ERR("FileBrowser", "Failed to rename file: %s -> %s", oldPath.c_str(), newPath.c_str());
    rollBackStatePath(oldBookmarkPath, newBookmarkPath, bookmarksMoved);
    rollBackStatePath(oldCachePath, newCachePath, cacheMoved);
    return {};
  }

  RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath);
  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    if (!APP_STATE.saveToFile()) LOG_ERR("FileBrowser", "Failed to save renamed open-book path");
  }

  return newPath;
}
}  // namespace

// To avoid traversing directories twice (once for cache clearing, once for deletion),
// we do both in one pass here, instead of using Storage.removeDir
bool remove(const std::string& fullPath) {
  // Never the card itself: a path that comes down to the root would take every book with it.
  if (FsHelpers::normalisePath(fullPath).empty()) {
    LOG_ERR("FileBrowser", "Refused to delete the card root: '%s'", fullPath.c_str());
    return false;
  }
  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("FileBrowser", "Failed to open for metadata clearing: %s", fullPath.c_str());
    return false;
  }

  if (!file.isDirectory()) {
    file.close();
    clearBookCache(fullPath);
    return Storage.remove(fullPath.c_str());
  }
  file.close();

  auto fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "OOM: delete name buffer");
    return false;
  }

  // Stack of (dirPath, postOrder): postOrder=true means rmdir this path after children are processed.
  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({fullPath, false});

  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();

    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) {
        LOG_ERR("FileBrowser", "Failed to rmdir: %s", currentPath.c_str());
        return false;
      }
      continue;
    }

    auto dir = Storage.open(currentPath.c_str());
    if (!dir) {
      LOG_ERR("FileBrowser", "Failed to open dir: %s", currentPath.c_str());
      return false;
    }
    if (!dir.isDirectory()) {
      LOG_ERR("FileBrowser", "Not a directory: %s", currentPath.c_str());
      return false;
    }

    // Push this dir for post-order rmdir (after all children are processed).
    stack.push_back({currentPath, true});

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) {
        continue;
      }
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') {
        entryPath += "/";
      }
      entryPath += fileNameBuffer.get();

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else {
        clearBookCache(entryPath);
        if (!Storage.remove(entryPath.c_str())) {
          LOG_ERR("FileBrowser", "Failed to remove file: %s", entryPath.c_str());
          return false;
        }
      }
    }
  }

  return true;
}

void confirmDelete(Activity& host, GfxRenderer& renderer, MappedInputManager& input, const std::string& path,
                   std::function<void()> deleted) {
  // A folder's path ends with '/': the question names the folder.
  const size_t end = path.size() > 1 && path.back() == '/' ? path.size() - 1 : path.size();
  const std::string name = utf8ComposeNfc(path.substr(path.find_last_of('/', end - 1) + 1, end - path.find_last_of('/', end - 1) - 1));
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(
      renderer, input, tr(STR_DELETE) + std::string("? "), name,
      ConfirmationContent{tr(STR_ALERT_FILE_QUESTION), name + " " + tr(STR_ALERT_FILE_MESSAGE_SUFFIX),
                          tr(STR_ALERT_FILE_ACTION_SHORT), tr(STR_ALERT_FILE_ACTION_LONG), "", true});
  if (!confirmation) {
    LOG_ERR("FileBrowser", "OOM: delete confirmation");
    return;
  }
  host.startActivityForResult(std::move(confirmation), [path, deleted = std::move(deleted)](const ActivityResult& res) {
    if (res.isCancelled) return;
    if (!remove(path)) {
      LOG_ERR("FileBrowser", "Failed to delete: %s", path.c_str());
      return;
    }
    if (deleted) deleted();
  });
}

void rename(Activity& host, GfxRenderer& renderer, MappedInputManager& input, const std::string& path,
            std::function<void(const std::string&)> renamed) {
  const std::string entry = path.substr(path.find_last_of('/') + 1);
  const std::string extension = getFileExtension(entry);
  const std::string initialStem = utf8ComposeNfc(entry.substr(0, entry.size() - extension.size()));
  const size_t maxStemLength = NAME_BUFFER_SIZE - extension.size() - 1;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, input, tr(STR_RENAME), initialStem,
                                                           maxStemLength, InputType::Text);
  if (!keyboard) {
    LOG_ERR("FileBrowser", "OOM: rename keyboard");
    return;
  }
  host.startActivityForResult(std::move(keyboard), [path, entry, extension, renamed = std::move(renamed)](
                                                       const ActivityResult& result) {
    if (result.isCancelled) return;
    const std::string newPath = moveFile(path, entry, std::get<KeyboardResult>(result.data).text, extension);
    if (!newPath.empty() && renamed) renamed(newPath);
  });
}
}  // namespace fileactions
