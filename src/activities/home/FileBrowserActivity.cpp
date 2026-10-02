#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <functional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DocThuMuc.h"
#include "FileFavorites.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookmarkUtil.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
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
}  // namespace

std::string getFileExtension(const std::string& filename);

void formatFileName(const std::string& filename, char* buffer, const size_t bufferSize) {
  if (filename.empty()) {
    buffer[0] = '\0';
    return;
  }
  const bool isDirectory = filename.back() == '/';
  const size_t dot = isDirectory ? filename.size() - 1 : filename.rfind('.');
  const int length = static_cast<int>(dot == std::string::npos ? filename.size() : dot);
  const char* format = isDirectory && !UITheme::getInstance().getTheme().showsFileIcons() ? "[%.*s]" : "%.*s";
  snprintf(buffer, bufferSize, format, length, filename.c_str());
  // Compose only the display copy; filesystem lookup needs the raw entry bytes.
  utf8ComposeNfcInPlace(buffer);
}

void formatFileExtension(const std::string& filename, char* buffer, const size_t bufferSize) {
  buffer[0] = '\0';
  if (filename.empty() || filename.back() == '/') return;
  if (const char* extension = strrchr(filename.c_str(), '.')) snprintf(buffer, bufferSize, "%s", extension);
}

FileBrowserActivity::FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string initialPath, const Mode mode)
    : UiListActivity("FileBrowser", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      mode(mode),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)),
      entryPath(basepath) {}

void FileBrowserActivity::loadFiles() {
  prewarmedStart = -1;  // new folder contents: re-prewarm the visible window
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    files.clear();
    return;
  }
  // Cai gi la muc doc duoc, cai gi bi giau, thu tu sap xep: chot o docthumuc, dung chung
  // voi the Folder cua man chinh.
  docthumuc::doc(basepath.c_str(), SETTINGS.showHiddenFiles,
                 mode == Mode::PickFirmware ? docthumuc::Loc::Firmware : docthumuc::Loc::Sach, fileNameBuffer.get(),
                 NAME_BUFFER_SIZE, files);
}

// fui::ListProps::rowProvider - formats row `index` from files[index] into the
// activity's scratch buffers on demand. Runs on the render task only for the
// rows the list actually lays out, so nothing per-file is materialized beyond
// `files` itself. The label/value pointers stay valid until the next call,
// which is all the provider contract requires. The "[folder]" format is
// theme-dependent, but it's re-derived here on every repaint, so a theme
// change picked up while this activity was paused underneath another screen
// needs no cache invalidation. A pinned row (Home Favourites) gets its glyph
// prefixed here too: there is no materialized ListItem array left for
// decoratePinnedRows() to patch after the fact.
void FileBrowserActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<FileBrowserActivity*>(ctx);
  if (index >= self->files.size()) return;
  const std::string& entry = self->files[index];
  formatFileName(entry, self->rowNameBuf, sizeof(self->rowNameBuf));
  if (self->rowIsPinned(static_cast<int>(index))) {
    static constexpr char PIN_GLYPH[] = "\xEE\x84\x8A";
    static constexpr size_t PIN_LEN = sizeof(PIN_GLYPH) - 1;
    const size_t nameLen = strlen(self->rowNameBuf);
    const size_t cap = sizeof(self->rowNameBuf);
    const size_t keep = std::min(nameLen, cap - 1 - PIN_LEN);
    memmove(self->rowNameBuf + PIN_LEN, self->rowNameBuf, keep);
    memcpy(self->rowNameBuf, PIN_GLYPH, PIN_LEN);
    self->rowNameBuf[PIN_LEN + keep] = '\0';
  }
  item.label = self->rowNameBuf;
  formatFileExtension(entry, self->rowExtBuf, sizeof(self->rowExtBuf));
  if (self->rowExtBuf[0] != '\0') {
    item.value = self->rowExtBuf;
  }
  item.icon = listIconFor(UITheme::getFileIcon(entry), 32);
  item.actionValue = static_cast<int16_t>(index);
}

// Batch-prewarm the CJK fallback glyphs for a bounded window of display names
// starting at the viewport top - one SD pass per list page (the reader TOC's
// refreshTocWindow pattern) instead of one unbounded pass over the whole
// folder. Getter form: no concatenated copy (a bare-new string append aborts
// under heap pressure). The last index covers the bottom path band: basepath
// (possibly a CJK folder name) draws in the same small font, so it must live
// in the same batch or it would evict the rows' glyphs when the heap gate
// disables union merging. (prewarmFallbackText appends the truncation
// ellipsis.)
void FileBrowserActivity::prewarmRowGlyphs(const int start) {
  const int total = static_cast<int>(files.size());
  int clamped = start;
  if (clamped > total - PREWARM_WINDOW) clamped = total - PREWARM_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == prewarmedStart) return;
  prewarmedStart = clamped;
  const int count = total - clamped < PREWARM_WINDOW ? total - clamped : PREWARM_WINDOW;

  struct PrewarmCtx {
    FileBrowserActivity* self;
    int first;
    int count;
  } prewarmCtx{this, clamped, count};
  renderer.prewarmFallbackText(
      uiScaleSpec().bodyFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        auto* c = const_cast<PrewarmCtx*>(static_cast<const PrewarmCtx*>(ctx));
        if (i < static_cast<uint32_t>(c->count)) {
          formatFileName(c->self->files[c->first + i], c->self->rowNameBuf, sizeof(c->self->rowNameBuf));
          return c->self->rowNameBuf;
        }
        return c->self->basepath.c_str();
      },
      &prewarmCtx, static_cast<uint32_t>(count) + 1);
}

void FileBrowserActivity::onEnter() {
  UiListActivity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    // The first screen build pulls the viewport to it (ListNav follow-on-build).
    nav.selected = static_cast<int>(findEntry(fileName));
  } else {
    loadFiles();
  }
}

void FileBrowserActivity::onExit() {
  rememberDirectory();
  Activity::onExit();
  files.clear();
  fileNameBuffer.reset();
}

// To avoid traversing directories twice (once for cache clearing, once for deletion),
// we do both in one pass here, instead of using Storage.removeDir
bool FileBrowserActivity::removeDirFile(const std::string& fullPath) {
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

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
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

void FileBrowserActivity::activateIndex(const int index) {
  (void)index;  // base already synced nav.selected to the tapped row
  // Activation navigates or opens; a lingering flash would gray an unrelated
  // row on the next list.
  app.clearTapFlash();
  activateSelected();
}

void FileBrowserActivity::onRowLongPress(const int index) {
  (void)index;  // base already synced nav.selected to the pressed row
  app.clearTapFlash();
  if (mode == Mode::Books) {
    showEntryActions();
  } else {
    activateSelected();
  }
}

void FileBrowserActivity::activateSelected() {
  if (files.empty()) return;
  // A touch activation can carry a row index captured before a delete/reload
  // shrank the list; the next render re-registers the rows.
  if (nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string& entry = files[nav.selected];
  bool isDirectory = (entry.back() == '/');

  // Firmware picker: select file -> return path; navigate into directories normally.
  if (mode == Mode::PickFirmware && !isDirectory) {
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    ActivityResult res{FilePathResult{cleanBasePath + entry}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  // buildScreen() runs on the render task and reads basepath plus `files`
  // through the row provider; mutate only under the render lock.
  RenderLock lock(*this);
  if (isDirectory) {
    rememberDirectory();
    const std::string parent = "BrowserDir:" + basepath;
    if (basepath.back() != '/') basepath += "/";
    basepath += entry.substr(0, entry.length() - 1);
    loadFiles();
    nav.selected = 0;
    nav.top = 0;
    if (mode == Mode::Books) {
      activityManager.menuNavigationMemory().enter(parent, "BrowserDir:" + basepath);
      restoreDirectory();
    }
    lock.unlock();
    requestUpdate();
  } else {
    const std::string fullPath = basepath + (basepath.back() == '/' ? "" : "/") + entry;
    lock.unlock();  // onSelectBook launches an activity; don't hold the lock across it
    onSelectBook(fullPath);
  }
}

void FileBrowserActivity::showEntryActions() {
  if (mode != Mode::Books || files.empty() || optionPopup.isActive() || nav.selected < 0 ||
      nav.selected >= listCount()) {
    return;
  }

  const bool isDirectory = files[nav.selected].back() == '/';
  static constexpr StrId FILE_OPTIONS[] = {StrId::STR_OPEN, StrId::STR_DELETE, StrId::STR_RENAME};
  static constexpr StrId DIRECTORY_OPTIONS[] = {StrId::STR_OPEN, StrId::STR_DELETE};
  optionPopup.show(StrId::STR_FILENAME, isDirectory ? DIRECTORY_OPTIONS : FILE_OPTIONS, isDirectory ? 2 : 3, 0,
                   [this](const int index) {
                     if (index == 0) {
                       activateSelected();
                     } else if (index == 1) {
                       deleteSelected();
                     } else if (index == 2) {
                       startRename();
                     }
                   });
  requestUpdate();
}

void FileBrowserActivity::deleteSelected() {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;

  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";
  const std::string entry = files[nav.selected];
  const std::string fullPath = cleanBasePath + entry;

  auto handler = [this, fullPath](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      return;
    }

    LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
    if (!removeDirFile(fullPath)) {
      LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
      return;
    }

    LOG_DBG("FileBrowser", "Deleted successfully");
    {
      RenderLock lock(*this);
      loadFiles();
      if (files.empty()) {
        nav.selected = 0;
      } else if (nav.selected >= listCount()) {
        nav.selected = listCount() - 1;
      }
      nav.follow(listCount());
    }
    requestUpdate(true);
  };

  const std::string heading = tr(STR_DELETE) + std::string("? ");
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, heading, utf8ComposeNfc(entry));
  if (!confirmation) {
    LOG_ERR("FileBrowser", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), std::move(handler));
}

void FileBrowserActivity::startRename() {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string oldEntry = files[nav.selected];
  if (oldEntry.back() == '/') return;

  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";
  const std::string oldPath = cleanBasePath + oldEntry;
  const std::string extension = getFileExtension(oldEntry);
  const std::string initialStem = utf8ComposeNfc(oldEntry.substr(0, oldEntry.size() - extension.size()));
  const size_t maxStemLength = NAME_BUFFER_SIZE - extension.size() - 1;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RENAME), initialStem,
                                                           maxStemLength, InputType::Text);
  if (!keyboard) {
    LOG_ERR("FileBrowser", "OOM: rename keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, oldPath, oldEntry, extension](const ActivityResult& result) {
    if (result.isCancelled) return;
    renameSelectedFile(oldPath, oldEntry, std::get<KeyboardResult>(result.data).text, extension);
  });
}

void FileBrowserActivity::renameSelectedFile(const std::string& oldPath, const std::string& oldEntry,
                                             const std::string& newStem, const std::string& extension) {
  const std::string newEntry = newStem + extension;
  if (newStem.empty() || newEntry.size() >= NAME_BUFFER_SIZE || !FsHelpers::isSafePathComponent(newEntry)) {
    LOG_ERR("FileBrowser", "Invalid rename target: %s", newEntry.c_str());
    return;
  }
  if (newEntry == utf8ComposeNfc(oldEntry)) return;

  const std::string parentPath = FsHelpers::extractFolderPath(oldPath);
  const std::string newPath = (parentPath == "/" ? parentPath : parentPath + "/") + newEntry;
  if (Storage.exists(newPath.c_str())) {
    LOG_ERR("FileBrowser", "Rename target already exists: %s", newPath.c_str());
    return;
  }

  const std::string oldCachePath = getBookCachePath(oldPath);
  const std::string newCachePath = getBookCachePath(newPath);
  const bool isEpub = FsHelpers::hasEpubExtension(oldPath);
  const std::string oldBookmarkPath = isEpub ? BookmarkUtil::getBookmarkPath(oldPath) : "";
  const std::string newBookmarkPath = isEpub ? BookmarkUtil::getBookmarkPath(newPath) : "";
  bool cacheMoved = false;
  bool bookmarksMoved = false;
  if (!moveStatePath(oldCachePath, newCachePath, cacheMoved)) return;
  if (!moveStatePath(oldBookmarkPath, newBookmarkPath, bookmarksMoved)) {
    rollBackStatePath(oldCachePath, newCachePath, cacheMoved);
    return;
  }
  if (!Storage.rename(oldPath.c_str(), newPath.c_str())) {
    LOG_ERR("FileBrowser", "Failed to rename file: %s -> %s", oldPath.c_str(), newPath.c_str());
    rollBackStatePath(oldBookmarkPath, newBookmarkPath, bookmarksMoved);
    rollBackStatePath(oldCachePath, newCachePath, cacheMoved);
    return;
  }

  RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath);
  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    if (!APP_STATE.saveToFile()) LOG_ERR("FileBrowser", "Failed to save renamed open-book path");
  }

  {
    RenderLock lock(*this);
    loadFiles();
    nav.selected = static_cast<int>(findEntry(newEntry));
    nav.follow(listCount());
  }
  requestUpdate(true);
}

bool FileBrowserActivity::handleCustomInput() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;

  // Holding PageBack is the hardware-button shortcut for delete: it reaches the
  // same confirm dialog as Delete in the entry-actions popup, for devices with
  // no touch to long-press a row.
  if (mode == Mode::Books && mappedInput.wasLongPressed(MappedInputManager::Button::PageBack, 700)) {
    deleteSelected();
    return true;
  }
  if (mode == Mode::Books && mappedInput.wasLongPressed(MappedInputManager::Button::PageForward, 400)) {
    if (nav.selected >= 0 && nav.selected < listCount() && files[nav.selected].back() != '/') {
      const auto path = basepath + (basepath.back() == '/' ? "" : "/") + files[nav.selected];
      if (FsHelpers::hasEpubExtension(path) || FsHelpers::hasTxtExtension(path) ||
          FsHelpers::hasMarkdownExtension(path) || FsHelpers::hasXtcExtension(path))
        startActivityForResult(ReaderActivity::create(renderer, mappedInput, path, false, true), nullptr);
    }
    return true;
  }

  // Long press BACK (1s+) goes to root folder (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  if (mode == Mode::Books && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= GO_HOME_MS && basepath != "/") {
    {
      // buildScreen() runs on the render task and reads basepath plus `files`
      // through the row provider; mutate only under the render lock.
      RenderLock lock(*this);
      basepath = "/";
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
    }
    return true;
  }
  // A hold leaves the browser directly; consume its release before Home handles input.
  if (mode == Mode::Books && mappedInput.wasLongPressed(MappedInputManager::Button::Back, GO_HOME_MS)) {
    activityManager.goHome(HomeMenuItem::FILE_BROWSER);
    return true;
  }

  return false;
}

bool FileBrowserActivity::handleButtons() {
  using Button = MappedInputManager::Button;
  if (mode == Mode::Books && basepath != "/" &&
      (mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Down))) {
    const int direction = mappedInput.wasReleased(Button::Down) ? 1 : -1;
    {
      RenderLock lock(*this);
      const auto next =
          docthumuc::neighbor(basepath, SETTINGS.showHiddenFiles, direction, fileNameBuffer.get(), NAME_BUFFER_SIZE);
      if (!next.empty() && next != basepath) {
        rememberDirectory();
        basepath = next;
        loadFiles();
        nav.reset();
        restoreDirectory();
        nav.followOnBuild = true;
      }
    }
    requestUpdate();
    return true;
  }
  if (mode == Mode::Books && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, GO_HOME_MS)) {
    app.clearTapFlash();
    showEntryActions();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        {
          // buildScreen() runs on the render task and reads basepath plus `files`
          // through the row provider; mutate only under the render lock.
          RenderLock lock(*this);
          rememberDirectory();
          basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
          if (basepath.empty()) basepath = "/";
          loadFiles();

          const auto pos = oldPath.find_last_of('/');
          const std::string dirName = oldPath.substr(pos + 1) + "/";
          nav.selected = static_cast<int>(findEntry(dirName));
          nav.top = 0;
          nav.follow(listCount());
        }

        requestUpdate();
      } else if (mode == Mode::PickFirmware) {
        // Firmware picker at root: cancel back to caller instead of going home.
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        onGoHome();
      }
    }
    return true;
  }

  return false;
}

void FileBrowserActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

std::string getFileExtension(const std::string& filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

void FileBrowserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (mode == Mode::Books && !SETTINGS.globalStatusBarHidden()) {
    const int actions = tenorchrome::tipHeight(renderer, tr(STR_FILE_SIDE_ACTIONS), 3);
    screen.takeBottom(static_cast<int16_t>(28 + actions));
  }

  // Full path band at the bottom: separator on top, left-truncated so the
  // deepest directory stays visible.
  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const fui::Rect band = screen.takeBottom(static_cast<int16_t>(pathLineHeight + metrics.verticalSpacing));
  const auto drawPath = [&]() {
    screen.target().fill(fui::Rect{band.x, band.y, band.width, 3}, fui::Paint::solid(fui::Color::Black));
    const int pathY =
        band.y + metrics.verticalSpacing / 2 + (band.height - metrics.verticalSpacing / 2 - pathLineHeight) / 2;
    const int pathMaxWidth = band.width - metrics.contentSidePadding * 2;
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, band.x + metrics.contentSidePadding, pathY, pathDisplay);
  };

  if (files.empty()) {
    drawPath();
    screen.centeredText(mode == Mode::PickFirmware ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND),
                        screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.rowProvider = &FileBrowserActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(files.size());
  props.action = ACTION_ROW;
  // Tap opens/navigates; long-press shows entry actions (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;  // air between the extension and the row edge
  // Names use up to two small-font lines; shared list layout sizes each row.
  fui::TextStyle label = uiMenuLabelText(screen.theme());
  label.maxLines = 2;
  props.labelText = label;

  // The trailing value here is just the short extension: skip the balanced
  // 60%-band wrap cap and let both name lines run the full width before it.
  props.balanceWrappedLabelWithValue = false;
  // Wrapped two-line names shrink how many rows fit a page, so the last row
  // of a page can end up in leftover space: draw it as a partial preview so
  // files past the fold are visibly present, not silently absent.
  props.partialTrailingRow = true;
  syncListViewport(screen, props);
  // Prewarm the window at the final viewport (syncListViewport just applied
  // follow/clamping to nav.top) before the list resolves rows through the
  // provider.
  prewarmRowGlyphs(nav.top);
  drawPath();
  screen.list(props);
}

void FileBrowserActivity::drawChrome() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const std::string label = tr(STR_HOME_TAB_FOLDER);
  const bool root = basepath == "/";
  const std::string folderName = mode == Mode::PickFirmware ? tr(STR_SELECT_FIRMWARE_FILE)
                                                            : (root ? label : basepath.substr(basepath.rfind('/') + 1));
  const std::string prefix = mode == Mode::Books && !root ? label + basepath.substr(0, basepath.rfind('/')) : "";
  if (tenorchrome::enabled()) {
    tenorchrome::drawHeader(renderer, folderName.c_str(), prefix.c_str());
  } else {
    const std::string title = prefix.empty() ? folderName : prefix + "/" + folderName;
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());
  }
}

void FileBrowserActivity::drawFooter() {
  if (mode == Mode::Books && !files.empty()) tenorchrome::drawTip(renderer, tr(STR_FILE_SIDE_ACTIONS), 1, 3);
  const char* backLabel = tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && !files.empty() && nav.selected >= 0 &&
                                     nav.selected < listCount() && files[nav.selected].back() != '/';
  const char* confirmLabel = files.empty() ? "" : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  const auto entry = std::find(files.begin(), files.end(), name);
  return entry != files.end() ? static_cast<size_t>(entry - files.begin()) : 0;
}

void FileBrowserActivity::captureNavigation(MenuNavigationState& state) const {
  UiListActivity::captureNavigation(state);
  state.location = basepath;
  if (nav.selected >= 0 && nav.selected < listCount()) state.selection = files[nav.selected];
}
void FileBrowserActivity::restoreNavigation(const MenuNavigationState& state) {
  if (mode != Mode::Books || state.location.empty()) return;
  RenderLock lock(*this);
  // The explicit destination wins over a previous visit's final directory.
  if (state.location != basepath) {
    restoreDirectory();
    return;
  }
  nav.selected = kepConTro(state.cursors[0].selected, listCount());
  if (!state.selection.empty()) {
    const auto it = std::find(files.begin(), files.end(), state.selection);
    if (it != files.end()) nav.selected = static_cast<int>(it - files.begin());
  }
  nav.top = state.cursors[0].top;
  nav.followOnBuild = true;
}
void FileBrowserActivity::rememberDirectory() {
  if (mode != Mode::Books) return;
  MenuNavigationState state;
  captureNavigation(state);
  activityManager.menuNavigationMemory().save("BrowserDir:" + basepath, state);
}
void FileBrowserActivity::restoreDirectory() {
  // Caller already holds RenderLock and loaded this directory.
  MenuNavigationState state;
  if (!activityManager.menuNavigationMemory().load("BrowserDir:" + basepath, state)) return;
  const auto it = std::find(files.begin(), files.end(), state.selection);
  nav.selected =
      it != files.end() ? static_cast<int>(it - files.begin()) : kepConTro(state.cursors[0].selected, listCount());
  nav.top = state.cursors[0].top;
  nav.followOnBuild = true;
}

std::string FileBrowserActivity::favoriteKey(int row) const {
  if (mode != Mode::Books || row < 0 || row >= listCount()) return {};
  auto name = files[row];
  const bool folder = !name.empty() && name.back() == '/';
  if (folder) name.pop_back();
  return filefavorites::keyFor(basepath + (basepath.back() == '/' ? "" : "/") + name, folder);
}
bool FileBrowserActivity::toggleFavorite(int row) {
  if (row < 0 || row >= listCount()) return false;
  auto name = files[row];
  const bool folder = name.back() == '/';
  if (folder) name.pop_back();
  return filefavorites::toggle(basepath + (basepath.back() == '/' ? "" : "/") + name, folder);
}
