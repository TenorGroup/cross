#include "FileBrowserActivity.h"
#include "X3FileReaderChrome.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <functional>

#include "CrossPointSettings.h"
#include "DocThuMuc.h"
#include "FileActions.h"
#include "FileBrowserState.h"
#include "FileFavorites.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;

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

std::string fileSearchStem(const std::string& entry) {
  if (entry.empty()) return {};
  std::string stem = entry;
  const bool directory = stem.back() == '/';
  if (directory) stem.pop_back();
  if (stem.find('/') != std::string::npos) return {};
  const size_t dot = directory ? std::string::npos : stem.rfind('.');
  if (dot != std::string::npos) stem.resize(dot);
  return stem;
}

bool fileSearchMatches(const std::string& entry, const std::string& query) {
  return filesearch::matchRank(entry, query) != 255;
}

void formatFileExtension(const std::string& filename, char* buffer, const size_t bufferSize) {
  buffer[0] = '\0';
  if (filename.empty() || filename.back() == '/') return;
  if (const char* extension = strrchr(filename.c_str(), '.')) snprintf(buffer, bufferSize, "%s", extension);
}

FileBrowserActivity::FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string initialPath, const Mode mode, const bool openedFromUglyFile)
    : UiListActivity("FileBrowser", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      mode(mode),
      openedFromUglyFile(openedFromUglyFile),
      // Opening File at its root lands in the last folder visited, kept across sleep (filebrowser::state).
      restoreLastPath(initialPath.empty() || initialPath == "/"),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)),
      entryPath(basepath) {}

void FileBrowserActivity::loadFiles() {
  stopRecursiveSearch();
  recursiveSearch = false;
  searchIncomplete = false;
  prewarmedStart = -1;  // new folder contents: re-prewarm the visible window
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    files.clear();
    searchIndices.clear();
    return;
  }
  // Cai gi la muc doc duoc, cai gi bi giau, thu tu sap xep: chot o docthumuc, dung chung
  // voi the Folder cua man chinh.
  // A folder can hold any number of names, the heap cannot: read up to a ceiling drawn from the heap left,
  // and refuse a folder past it whole, with a line saying so.
  const size_t cap = docthumuc::tran(ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  tooMany = false;
  if (cap == 0) {
    files.clear();
    searchIndices.clear();
    tooMany = true;
    return;
  }
  files.clear();
  docthumuc::doc(basepath.c_str(), SETTINGS.showHiddenFiles,
                 mode == Mode::PickFirmware ? docthumuc::Loc::Firmware : docthumuc::Loc::Sach, fileNameBuffer.get(),
                 NAME_BUFFER_SIZE, files, cap, &tooMany, sortMode);
  searchIndices.clear();
  if (!searchQuery.empty()) startRecursiveSearch(searchQuery);
#ifdef TENOR_PRESS_PROBE
  LOG_INF("FILES_SEARCH", "source_n=%u result_n=%u alloc=%u free=%u largest=%u",
          static_cast<unsigned>(files.size()), static_cast<unsigned>(listCount()),
          static_cast<unsigned>(searchIndices.capacity() * sizeof(uint16_t)),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  if (tooMany) LOG_INF("FILES", "%s holds more than %u names: refused", basepath.c_str(), static_cast<unsigned>(cap));
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
  if (index < kSearchRows) {
    item.label = tr(STR_SEARCH);
    item.actionValue = 0;
    item.opensNext = true;
    return;
  }
  const size_t fileIndex = static_cast<size_t>(index - kSearchRows);
  if (fileIndex >= static_cast<size_t>(self->listCount())) return;
  const std::string& entry = self->entryForVisibleRow(static_cast<int>(fileIndex));
  if (self->recursiveSearch)
    formatFileName(std::string(filesearch::basename(entry)), self->rowNameBuf, sizeof(self->rowNameBuf));
  else formatFileName(entry, self->rowNameBuf, sizeof(self->rowNameBuf));
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
  if (self->recursiveSearch && tenorchrome::kTouchShell && filesearch::parent(entry) != self->basepath) {
    const auto folder = filesearch::basename(filesearch::parent(entry));
    if (!folder.empty()) {
      snprintf(self->rowSubtitleBuf, sizeof(self->rowSubtitleBuf), "%.*s", static_cast<int>(folder.size()), folder.data());
      utf8ComposeNfcInPlace(self->rowSubtitleBuf);
      item.subtitle = self->rowSubtitleBuf;
    }
  }
  formatFileExtension(entry, self->rowExtBuf, sizeof(self->rowExtBuf));
  if (self->rowExtBuf[0] != '\0' && !tenorchrome::kTouchShell) {
    item.value = self->rowExtBuf;
  }
  item.icon = listIconFor(UITheme::getFileIcon(entry), 32);
  item.actionValue = static_cast<int16_t>(index);
  item.opensNext = tenorchrome::kTouchShell && self->rowOpens(static_cast<int>(index));
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
  const int total = listCount();
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
        const int row = c->first + static_cast<int>(i);
        if (row < kSearchRows) return tr(STR_SEARCH);
        if (row >= kSearchRows && row < c->self->listCount()) {
          const auto& raw = c->self->entryForVisibleRow(row);
          if (c->self->recursiveSearch)
            formatFileName(std::string(filesearch::basename(raw)), c->self->rowNameBuf, sizeof(c->self->rowNameBuf));
          else formatFileName(raw, c->self->rowNameBuf, sizeof(c->self->rowNameBuf));
          return c->self->rowNameBuf;
        }
        return c->self->basepath.c_str();
      },
      &prewarmCtx, static_cast<uint32_t>(count) + 1);
}

void FileBrowserActivity::applySearch(const std::string& query) {
  prewarmedStart = -1;
  stopRecursiveSearch();
  recursiveSearch = false;
  searchIncomplete = false;
  searchIndices.clear();
  if (!query.empty() && !filesearch::buildResults(files, query, searchIndices)) {
    LOG_ERR("FileBrowser", "Search result allocation refused");
    searchIncomplete = true;
  }
}

void FileBrowserActivity::startRecursiveSearch(const std::string& query) {
  stopRecursiveSearch();
  recursiveSearch = false;
  searchIncomplete = false;
  searchIndices.clear();
  searchCancelled = false;
  searchStorage = makeUniqueNoThrow<filesearch::HalStorageAdapter>();
  searchHooks = makeUniqueNoThrow<filesearch::HalScanHooks>(searchCancelled, SETTINGS.showHiddenFiles, mappedInput, mode == Mode::PickFirmware);
  searchScanner = makeUniqueNoThrow<filesearch::Scanner>();
  if (!searchStorage || !searchHooks || !searchScanner) {
    LOG_ERR("FileBrowser", "OOM: recursive file search");
    applySearch(query);
    searchIncomplete = true;
    return;
  }
  if (!searchScanner->start(*searchStorage, *searchHooks, basepath, query)) {
    LOG_ERR("FileBrowser", "Recursive file search refused: %u", static_cast<unsigned>(searchScanner->reason()));
    applySearch(query);
    searchIncomplete = true;
    return;
  }
}

void FileBrowserActivity::tickRecursiveSearch() {
  if (!searchScanner || searchScanner->reason() != filesearch::ScanReason::Running) return;
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) return;
  const auto reason = searchScanner->tick();
  if (reason == filesearch::ScanReason::Running || reason == filesearch::ScanReason::Cancelled) return;
  recursiveSearch = true;
  searchIncomplete = reason != filesearch::ScanReason::Complete;
  nav.reset();
  prewarmedStart = -1;
#ifdef TENOR_PRESS_PROBE
  LOG_INF("FILES_SEARCH", "source_n=%u result_n=%u alloc=%u free=%u largest=%u",
          unsigned(searchScanner->stats().entries), unsigned(searchScanner->resultCount()),
          unsigned(searchScanner->allocatedBytes()), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMaxAllocHeap()));
#endif
  requestUpdate();
}

void FileBrowserActivity::render(RenderLock&& lock) {
  if (searchScanner && searchScanner->reason() == filesearch::ScanReason::Running) return;
  UiListActivity::render(std::move(lock));
}

void FileBrowserActivity::stopRecursiveSearch() {
  if (searchScanner && searchScanner->reason() == filesearch::ScanReason::Running) {
    searchScanner->cancel();
    searchScanner->tick();
  }
  searchScanner.reset();
  searchHooks.reset();
  searchStorage.reset();
  searchCancelled = true;
}

void FileBrowserActivity::openSearch() {
  if (!canSearchFiles()) return;
  app.clearTapFlash();
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH), searchQuery, 48,
                                                           InputType::Text);
  if (!keyboard) {
    LOG_ERR("FileBrowser", "OOM: search keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    searchQuery = std::get<KeyboardResult>(result.data).text;
    if (searchQuery.empty()) {
      applySearch(searchQuery);
    } else {
      startRecursiveSearch(searchQuery);
    }
    nav.selected = 0;
    nav.top = 0;
    requestUpdate();
  });
}

void FileBrowserActivity::onEnter() {
  UiListActivity::onEnter();
  searchQuery.clear();
  searchIndices.clear();
  recursiveSearch = false;
  searchIncomplete = false;

  if (mode == Mode::Books) {
    const auto& saved = filebrowser::state();
    sortMode = shell::isUgly() ? saved.uglySort : saved.crossSort;
    if (restoreLastPath) {
      const std::string& savedPath = shell::isUgly() ? saved.uglyPath : saved.crossPath;
      auto savedDir = Storage.open(savedPath.c_str());
      if (savedDir && savedDir.isDirectory()) basepath = savedPath;
    }
  }

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
    nav.selected = static_cast<int>(findEntry(fileName)) + kSearchRows;
  } else {
    loadFiles();
  }
}

// Settings changes the order; the write waits for the next frame, as the File screen's own does.
filebrowser::SortMode filebrowser::settingsSort() { return sortNow(shell::isUgly()); }
void filebrowser::chooseSettingsSort(const SortMode mode) {
  chooseSort(shell::isUgly(), mode);
  activityManager.deferWrite(&flush);
}

void FileBrowserActivity::onExit() {
  stopRecursiveSearch();
  rememberDirectory();
  if (mode == Mode::Books) {
    filebrowser::note(shell::isUgly(), basepath, sortMode);
    // Sleep has already painted its entering notice and will turn storage off in this call.
    if (activityManager.isSleepTransition()) filebrowser::flush();
    else activityManager.deferWrite(&filebrowser::flush);
  }
  Activity::onExit();
  files.clear();
  searchIndices.clear();
  fileNameBuffer.reset();
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
  // A touch activation can carry a row index captured before a delete/reload
  // shrank the list; the next render re-registers the rows.
  if (nav.selected < 0 || nav.selected >= listCount()) return;

  if (nav.selected < kSearchRows) {
    openSearch();
    return;
  }

  const std::string& entry = entryForVisibleRow(nav.selected);
  const std::string fullPath = fullPathForVisibleRow(nav.selected);
  bool isDirectory = (entry.back() == '/');

  // Firmware picker: select file -> return path; navigate into directories normally.
  if (mode == Mode::PickFirmware && !isDirectory) {
    ActivityResult res{FilePathResult{fullPath}};
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
    searchQuery.clear();
    fileSearchReveal.reset();
    searchIndices.clear();
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
    lock.unlock();  // onSelectBook launches an activity; don't hold the lock across it
    onSelectBook(fullPath);
  }
}

void FileBrowserActivity::showEntryActions() {
  if (mode != Mode::Books || listCount() == 0 || rowMenu.isActive() || nav.selected < kSearchRows ||
      nav.selected >= listCount()) {
    return;
  }

  const bool isDirectory = entryForVisibleRow(nav.selected).back() == '/';
  // The same actions on both devices: Pin or Unpin, Rename (files), Delete. Select or a tap already opens.
  enum class Do : uint8_t { Pin, Delete, Rename };
  Do actions[3];
  StrId labels[3];
  int count = 0;
  const auto add = [&](const Do what, const StrId label) {
    actions[count] = what;
    labels[count++] = label;
  };
  add(Do::Pin, rowIsPinned(nav.selected) ? StrId::STR_UNPIN_FAVORITE : StrId::STR_PIN_FAVORITE);
  if (!isDirectory) add(Do::Rename, StrId::STR_RENAME);
  add(Do::Delete, StrId::STR_DELETE);
  const auto onSelect = [this, actions, count](const int index) {
    if (index < 0 || index >= count) return;
    switch (actions[index]) {
      case Do::Pin:
        queuePinToggle();
        break;
      case Do::Delete:
        deleteSelected();
        break;
      case Do::Rename:
        startRename();
        break;
    }
  };
  if (tenorchrome::kTouchShell) {
    showRowMenu(labels, count, onSelect, ACTION_ROW, nav.selected);
    return;
  }
  rowMenu.show(StrId::STR_FILENAME, labels, count, 0, onSelect);
  requestUpdate();
}

void FileBrowserActivity::deleteSelected() {
  if (listCount() == 0 || nav.selected < kSearchRows || nav.selected >= listCount()) return;
  fileactions::confirmDelete(*this, renderer, mappedInput, fullPathForVisibleRow(nav.selected), [this] {
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
  });
}

void FileBrowserActivity::startRename() {
  if (listCount() == 0 || nav.selected < kSearchRows || nav.selected >= listCount()) return;
  if (entryForVisibleRow(nav.selected).back() == '/') return;
  fileactions::rename(*this, renderer, mappedInput, fullPathForVisibleRow(nav.selected), [this](const std::string& newPath) {
    {
      RenderLock lock(*this);
      loadFiles();
      nav.selected = static_cast<int>(findEntry(newPath.substr(newPath.find_last_of('/') + 1))) + kSearchRows;
      nav.follow(listCount());
    }
    requestUpdate(true);
  });
}

bool FileBrowserActivity::handleCustomInput() {
  if (searchScanner && (searchScanner->reason() == filesearch::ScanReason::Running ||
                        searchScanner->reason() == filesearch::ScanReason::Cancelled) && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    RenderLock lock(*this);
    stopRecursiveSearch();
    searchQuery.clear();
    applySearch(searchQuery);
    nav.reset();
    requestUpdate();
    return true;
  }
  if (searchScanner && (searchScanner->reason() == filesearch::ScanReason::Running ||
                        searchScanner->reason() == filesearch::ScanReason::Cancelled)) {
    tickRecursiveSearch();
    return true;
  }
  // Holding PageBack is the hardware-button shortcut for delete: it reaches the
  // same confirm dialog as Delete in the entry-actions popup, for devices with
  // no touch to long-press a row.
  // Not on the touch shell: its rows have no cursor, so the held key would delete a row nobody chose.
  if (!tenorchrome::kTouchShell && mode == Mode::Books &&
      mappedInput.wasLongPressed(MappedInputManager::Button::PageBack, 700)) {
    deleteSelected();
    return true;
  }
  if (mode == Mode::Books && mappedInput.wasLongPressed(MappedInputManager::Button::PageForward, 400)) {
    if (nav.selected >= kSearchRows && nav.selected < listCount() && entryForVisibleRow(nav.selected).back() != '/') {
      const auto path = fullPathForVisibleRow(nav.selected);
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
      searchQuery.clear();
      fileSearchReveal.reset();
      searchIndices.clear();
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
        searchQuery.clear();
        fileSearchReveal.reset();
        searchIndices.clear();
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
      // Touch: the root of File is the File card of Home, so a folder right under it goes back there.
      if ((tenorchrome::kTouchShell || openedFromUglyFile) && mode == Mode::Books && basepath != "/" &&
          basepath.find('/', 1) == std::string::npos) {
        activityManager.goHome(HomeMenuItem::FILE_BROWSER);
      } else if (basepath != "/") {
        const std::string oldPath = basepath;

        {
          // buildScreen() runs on the render task and reads basepath plus `files`
          // through the row provider; mutate only under the render lock.
          RenderLock lock(*this);
          rememberDirectory();
          basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
          if (basepath.empty()) basepath = "/";
          searchQuery.clear();
          fileSearchReveal.reset();
          searchIndices.clear();
          loadFiles();

          const auto pos = oldPath.find_last_of('/');
          const std::string dirName = oldPath.substr(pos + 1) + "/";
          nav.selected = static_cast<int>(findEntry(dirName)) + kSearchRows;
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


std::string getFileExtension(const std::string& filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

void FileBrowserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool x3 = tenorchrome::x3ScreenLayoutEnabled() && !shell::uglyParts();
  if (x3) {
    const auto body = screenLayout().bodyRect;
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(body.y), 0,
                                                  static_cast<int16_t>(renderer.getScreenHeight() - body.bottom()), 0});
    if (favoriteSaveFailed) {
      const int height = tenorchrome::tipHeight(renderer, tr(STR_MENU_SAVE_FAILED));
      screen.takeBottom(static_cast<int16_t>(height + 8));
    }
  } else {
    // Content below the GUI.drawHeader band, above the button hints.
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
    screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  }

  buildFileSearch(screen);

  const bool uglyLayout = shell::uglyParts() && tenorchrome::x3ScreenLayoutEnabled();
  if (uglyLayout) {
    const auto layout = tenorchrome::x3ScreenLayout(renderer, mode == Mode::Books && !files.empty()
                                                               ? tenorchrome::tipHeight(renderer, tr(STR_FILE_SIDE_ACTIONS), 3) + 26
                                                               : 0);
    if (screen.body().bottom() > layout.bodyRect.bottom())
      screen.takeBottom(static_cast<int16_t>(screen.body().bottom() - layout.bodyRect.bottom()));
  } else if (!x3 && mode == Mode::Books && !SETTINGS.globalStatusBarHidden() && !tenorchrome::kTouchShell) {
    const int actions = tenorchrome::tipHeight(renderer, tr(STR_FILE_SIDE_ACTIONS), 3);
    screen.takeBottom(static_cast<int16_t>(28 + actions));
  }

  // Full path band at the bottom: separator on top, left-truncated so the
  // deepest directory stays visible.
  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  // Touch: the foot of the screen is for the tab bar, so no path band; the header names the folder.
  const fui::Rect band = tenorchrome::kTouchShell || x3 || uglyLayout
                             ? fui::Rect{}
                             : screen.takeBottom(static_cast<int16_t>(pathLineHeight + metrics.verticalSpacing));
  const auto drawPath = [&]() {
    if (tenorchrome::kTouchShell || x3 || uglyLayout) return;
    if (shell::uglyParts()) {
      // tenor/ugly: a shaky rule and the path in hand, cut on the left so the deepest folder stays.
      const int maxWidth = band.width - metrics.contentSidePadding * 2;
      const char* p = basepath.c_str();
      std::string shown = p;
      if (ugly::width(renderer, ugly::Size::S22, p) > maxWidth) {
        const int available = maxWidth - ugly::width(renderer, ugly::Size::S22, "\xEE\x80\x80");
        while (*p && ugly::width(renderer, ugly::Size::S22, p) > available) {
          ++p;
          while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
        }
        shown = std::string("\xEE\x80\x80") + p;  // the pen scrawl where a cut line trails off
      }
      ugly::line(renderer, band.x + 8, band.y + 2, band.x + band.width - 8, band.y + 1, 57, 2);
      ugly::text(renderer, ugly::Size::S22, band.x + metrics.contentSidePadding,
                 band.y + band.height - metrics.verticalSpacing / 2 - 4, shown.c_str());
      return;
    }
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

  if (searchIncomplete) {
    const auto band = screen.takeTop(static_cast<int16_t>(renderer.getLineHeight(SMALL_FONT_ID) + 8), 4);
    fui::TextStyle status = screen.theme().smallText;
    status.align = fui::TextAlign::Center;
    screen.target().text(band, tr(STR_SEARCH_INCOMPLETE), status);
  }

  if (listCount() == 0 && searchQuery.empty()) {
    drawPath();
    if (x3) return;
    screen.centeredText(tooMany ? tr(STR_FOLDER_TOO_MANY)
                                : mode == Mode::PickFirmware ? tr(STR_NO_BIN_FILES)
                                                             : tr(STR_NO_FILES_FOUND),
                        screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.rowProvider = &FileBrowserActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
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
  if (recursiveSearch) {
    props.subtitleText = screen.theme().smallText;
    props.subtitleText.maxLines = 1;
  }
  syncListViewport(screen, props, recursiveSearch);
  // Prewarm the window at the final viewport (syncListViewport just applied
  // follow/clamping to nav.top) before the list resolves rows through the
  // provider.
  prewarmRowGlyphs(nav.top);
  drawPath();
  if (listCount() == 0) {
    screen.list(props, static_cast<int16_t>(metrics.listRowHeight), fui::LayoutAnchor::Top);
    if (x3) {
      const auto body = screenLayout().bodyRect;
      const int rowHeight = metrics.listRowHeight;
      tenorchrome::drawInfoInRect(renderer, tr(STR_NO_FILES_FOUND),
                                  {body.x, body.y + rowHeight, body.width, body.height - rowHeight});
    } else
      screen.centeredText(tr(STR_NO_FILES_FOUND), screen.theme().bodyText);
    return;
  }
  screen.list(props);
}

void FileBrowserActivity::drawChrome() {
  if (tenorchrome::x3ScreenLayoutEnabled() && !shell::uglyParts()) {
    x3readerchrome::drawPath(renderer, screenLayout().barRect, icon_tenor_home_folder_40, basepath.c_str());
    return;
  }
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const std::string label = tr(STR_HOME_TAB_FOLDER);
  const bool root = basepath == "/";
  const std::string folderName = mode == Mode::PickFirmware ? tr(STR_SELECT_FIRMWARE_FILE)
                                                            : (root ? label : basepath.substr(basepath.rfind('/') + 1));
  const std::string prefix = mode == Mode::Books && !root ? label + basepath.substr(0, basepath.rfind('/')) : "";
  if (tenorchrome::enabled()) {
    tenorchrome::drawHeader(renderer, folderName.c_str(), prefix.c_str());
    if (tenorchrome::kTouchShell) tenorchrome::noteScreenTitle(basepath.c_str());
    // Touch: the status strip says how many items the folder holds.
    if (tenorchrome::kTouchShell && listCount() > 0) {
      char count[24];
      snprintf(count, sizeof(count), tr(STR_FOLDER_ITEM_COUNT), static_cast<unsigned>(listCount()));
      tenorchrome::noteStatus(count);
    }
  } else {
    const std::string title = prefix.empty() ? folderName : prefix + "/" + folderName;
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());
  }
}

void FileBrowserActivity::drawFooter() {
  if (shell::uglyParts() && tenorchrome::x3ScreenLayoutEnabled()) {
    const std::string path = std::string(tr(STR_HOME_TAB_FOLDER)) +
                             (basepath.empty() || basepath.front() != '/' ? "/" : "") + basepath;
    ugly::pathHints(renderer, path.c_str());
    if (mode == Mode::Books && !files.empty()) tenorchrome::drawTip(renderer, tr(STR_FILE_SIDE_ACTIONS), 1, 3);
  } else if (tenorchrome::x3ScreenLayoutEnabled()) {
    drawX3Content();
  } else if (mode == Mode::Books && !files.empty()) {
    tenorchrome::drawTip(renderer, tr(STR_FILE_SIDE_ACTIONS), 1, 3);
  }
  const char* backLabel = tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const bool selectingSearch = nav.selected >= 0 && nav.selected < kSearchRows;
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && nav.selected >= kSearchRows &&
                                     nav.selected < listCount() && entryForVisibleRow(nav.selected).back() != '/';
  const char* confirmLabel = selectingSearch ? tr(STR_SEARCH)
                                             : (listCount() <= 1 ? "" : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN)));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, listCount() <= 1 ? "" : tr(STR_DIR_UP),
                                            listCount() <= 1 ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

const char* FileBrowserActivity::favoriteHintText() {
  if (favoriteSaveFailed || mode != Mode::Books || nav.selected < kSearchRows || nav.selected >= listCount())
    return UiListActivity::favoriteHintText();
  return tr(STR_FILE_HOLD_ACTIONS);
}

std::string FileBrowserActivity::screenTip() {
  if (mode != Mode::Books || files.empty()) return {};
  std::string text = tr(STR_FILE_SIDE_ACTIONS);
  if (!favoriteSaveFailed) {
    if (const char* hint = favoriteHintText()) {
      text += "\n";
      text += hint;
    }
  }
  return text;
}

tenorchrome::X3ScreenLayout FileBrowserActivity::screenLayout() {
  const auto tip = screenTip();
  return tenorchrome::x3ScreenLayout(renderer, tenorchrome::tipHeight(renderer, tip.c_str(), 7));
}

void FileBrowserActivity::drawX3Content() {
  const auto layout = screenLayout();
  const auto tip = screenTip();
  tenorchrome::drawTipInRect(renderer, tip.c_str(), layout.tipRect, 7);
  if (files.empty() && searchQuery.empty())
    tenorchrome::drawInfoInRect(renderer,
                                tooMany                      ? tr(STR_FOLDER_TOO_MANY)
                                : mode == Mode::PickFirmware ? tr(STR_NO_BIN_FILES)
                                                             : tr(STR_NO_FILES_FOUND),
                                layout.bodyRect);
  if (favoriteSaveFailed) {
    const int height = tenorchrome::tipHeight(renderer, tr(STR_MENU_SAVE_FAILED));
    const auto body = layout.bodyRect;
    tenorchrome::drawInfoInRect(renderer, tr(STR_MENU_SAVE_FAILED),
                                {body.x, body.bottom() - height, body.width, height});
  }
}

const std::string& FileBrowserActivity::entryForVisibleRow(const int row) const {
  const size_t visible = static_cast<size_t>(row - kSearchRows);
  if (!searchQuery.empty() && recursiveSearch && searchScanner) {
    searchEntry = std::string(searchScanner->result(visible).path);
    return searchEntry;
  }
  return files[searchQuery.empty() ? visible : searchIndices[visible]];
}

std::string FileBrowserActivity::fullPathForVisibleRow(const int row) const {
  const auto& entry = entryForVisibleRow(row);
  if (!searchQuery.empty() && recursiveSearch) return entry;
  return basepath + (basepath.back() == '/' ? "" : "/") + entry;
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  const size_t count = searchQuery.empty() ? files.size() : (recursiveSearch && searchScanner ? searchScanner->resultCount() : searchIndices.size());
  for (size_t visible = 0; visible < count; ++visible) {
    if (entryForVisibleRow(static_cast<int>(visible) + kSearchRows) == name) return visible;
  }
  return 0;
}

void FileBrowserActivity::captureNavigation(MenuNavigationState& state) const {
  UiListActivity::captureNavigation(state);
  state.location = basepath;
  if (nav.selected >= kSearchRows && nav.selected < listCount()) state.selection = entryForVisibleRow(nav.selected);
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
    nav.selected = static_cast<int>(findEntry(state.selection)) + kSearchRows;
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
  nav.selected = state.selection.empty() ? kepConTro(state.cursors[0].selected, listCount())
                                         : static_cast<int>(findEntry(state.selection)) + kSearchRows;
  nav.top = state.cursors[0].top;
  nav.followOnBuild = true;
}

std::string FileBrowserActivity::favoriteKey(int row) const {
  if (mode != Mode::Books || row < kSearchRows || row >= listCount()) return {};
  auto name = fullPathForVisibleRow(row);
  const bool folder = !name.empty() && name.back() == '/';
  if (folder) name.pop_back();
  return filefavorites::keyFor(name, folder);
}
bool FileBrowserActivity::toggleFavorite(int row) {
  if (row < kSearchRows || row >= listCount()) return false;
  auto name = fullPathForVisibleRow(row);
  const bool folder = name.back() == '/';
  if (folder) name.pop_back();
  return filefavorites::toggle(name, folder);
}
