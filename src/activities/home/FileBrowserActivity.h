#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// The name a folder entry shows: no trailing slash, no extension (the File card of Home uses it too).
void formatFileName(const std::string& filename, char* buffer, size_t bufferSize);
std::string fileSearchStem(const std::string& entry);
bool fileSearchMatches(const std::string& entry, const std::string& query);

class FileBrowserActivity final : public UiListActivity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // File actions
  bool removeDirFile(const std::string& fullPath);
  void showEntryActions();
  void startRename();
  void renameSelectedFile(const std::string& oldPath, const std::string& oldEntry, const std::string& newStem,
                          const std::string& extension);
  void deleteSelected();

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::string entryPath;
  void rememberDirectory();
  void restoreDirectory();
  std::vector<std::string> files;
  // The raw current-directory names are retained only while a search is active,
  // so clearing the query restores the same directory without another SD walk.
  std::vector<std::string> searchSourceFiles;
  std::string searchQuery;
  std::unique_ptr<char[]> fileNameBuffer;

  // Pull-based rows: the SDK list resolves each drawn row on demand through
  // provideRow() (fui::ListProps::rowProvider), so the only per-file
  // residency is `files` itself - no full-length rowNames/rowExtensions/
  // rowItems arrays (a 1000-file folder used to pin ~100KB of vectors plus a
  // heap copy of every display name, which aborted under -fno-exceptions
  // when the contiguous blocks no longer fit). The label/value strings for
  // the row being laid out live in these scratch buffers; the provider
  // contract only needs them valid until the next provideRow() call. The
  // "[folder]" bracket formatting and the Home Favourites pin glyph are both
  // theme/state-dependent, but formatFileName() and provideRow() re-derive
  // them on every call, so there is no cache to invalidate when a theme
  // change or a pin toggle is picked up while this activity is paused
  // underneath another screen.
  static constexpr size_t ROW_NAME_BUF_SIZE = 512;  // NAME_BUFFER_SIZE + "[]" + terminator slack
  char rowNameBuf[ROW_NAME_BUF_SIZE]{};
  char rowExtBuf[16]{};
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  // CJK fallback glyphs are prewarmed for a bounded window of rows around the
  // viewport (one SD pass per list page, like the reader TOC) instead of the
  // whole folder. -1 = nothing prewarmed; reset by loadFiles().
  static constexpr int PREWARM_WINDOW = 24;
  int prewarmedStart = -1;
  void prewarmRowGlyphs(int start);

  int listCount() const override { return static_cast<int>(files.size()) + 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Long-press BACK goes to root; short Back goes up a directory (home/cancel at
  // root), and Confirm activates on release while a hold opens file actions.
  bool handleCustomInput() override;
  bool handleButtons() override;
  bool tooMany = false;  // the folder holds more names than the heap allows: nothing is listed
  bool supportsFavorites() const override { return mode == Mode::Books; }
  bool fileList() const override { return true; }
  bool rowOpens(int row) const override {
    return row == 0 || (row > 0 && row - 1 < static_cast<int>(files.size()) && files[row - 1].back() == '/');
  }
  std::string favoriteKey(int row) const override;
  bool toggleFavorite(int row) override;
  // Header shows the current folder name (battery indicator via GUI.drawHeader);
  // footer labels depend on path depth and picker mode.
  void drawChrome() override;
  void drawFooter() override;
  void activateSelected();

  // Data loading
  void loadFiles();
  void openSearch();
  void applySearch(const std::string& query);
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books);
  void onEnter() override;
  bool remembersNavigation() const override { return mode == Mode::Books; }
  std::string navigationMemoryKey() const override { return "FileBrowserEntry:" + entryPath; }
  void captureNavigation(MenuNavigationState& state) const override;
  void restoreNavigation(const MenuNavigationState& state) override;
  void onExit() override;
};
