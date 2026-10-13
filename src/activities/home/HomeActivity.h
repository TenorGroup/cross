#pragma once
#include <I18n.h>
#include <EpdFontFamily.h>

#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include "./FileBrowserActivity.h"
#include "components/FileSearchScan.h"
#include "components/FileSearchHalAdapter.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "activities/UiTabListActivity.h"
#include "activities/settings/SettingsTabs.h"
#include "components/HomeExcerptStyle.h"
#include "components/HomeSettingsLayout.h"
#include "components/HomeRecentLayout.h"
#include "HomeRefresh.h"
#include <HalMemory.h>
#include <IdleWork.h>

struct Rect;

// Home tabs retain logical IDs while the user changes their visual order.
// Side taps switch tabs; side holds move the active tab one position.
class HomeActivity final : public UiTabListActivity {
 public:
  // CAI_DAT chu khong phai SETTINGS: CrossPointSettings.h dinh nghia SETTINGS
  // thanh mot macro, nen Tab::SETTINGS no ra thanh mot loi bien dich kho doan.
  enum class Tab : uint8_t { RECENT, FOLDER, STATS, CAI_DAT, FAVORITES };
  static constexpr int TAB_COUNT = 5;

  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE, bool cleanInitialRefresh = false);
  void onEnter() override;
  void restoreNavigation(const MenuNavigationState& state) override;
  void captureNavigation(MenuNavigationState& state) const override;
  void onExit() override;
  void onPause() override;
  void onResume() override;
  void onTick() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
#ifdef TENOR_UI_ACCEPTANCE
  void stepForTest(int direction) {
    const int count = listCount();
    if (count <= 0) {
      moveRingTo(0);
      return;
    }
    const int ring = ringPos();
    if (direction > 0) {
      moveRingTo(ring <= 0 || ring >= count ? 1 : ring + 1);
    } else if (direction < 0) {
      moveRingTo(ring <= 1 || ring > count ? count : ring - 1);
    }
  }
  // Doi thang sang the thu `index` de nghiem thu chup du anh tung man. CHI co trong ban nghiem thu USB
  // (#ifdef TENOR_UI_ACCEPTANCE) - khong vao ban phat hanh, giu lai de con chup du sau man.
  void tabForTest(int index) {
    if (index < 0 || index >= TAB_COUNT) return;
    // selectTab da tu lay RenderLock; goi thang mot lan thay vi lap stepTab de moi lan doi chi mot lan rebuild.
    if (static_cast<int>(activeTabId) != index) selectTab(static_cast<Tab>(index));
    requestUpdate();
  }
#endif

 private:
  // --- UiTabListActivity contract ---
  int tabCount() const override { return TAB_COUNT; }
  int activeTab() const override { return static_cast<int>(activeTabId); }
  const char* tabLabel(int index) const override;
  freeink::ui::BitmapRef tabIcon(int index, bool bold) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;

  int listCount() const override {
    if (activeTabId == Tab::FOLDER && folderRecursiveSearch && folderSearchScanner)
      return static_cast<int>(folderSearchScanner->resultCount());
    return static_cast<int>(rowItems.size());
  }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  bool handleCustomInput() override;
  static constexpr freeink::ui::ActionId ACTION_OTHER_BOOK = ACTION_TAB_USER;
  static void otherBookTrampoline(const freeink::ui::ActionEvent& event, void* user) {
    auto* self = static_cast<HomeActivity*>(user);
    // Touch: a hold on the line offers the actions of the book it names, anchored to the line.
    if (event.longPress) {
      self->showOtherBookMenu();
      return;
    }
    self->queueNavIntent(NavIntent::StepNext);
  }
  void showOtherBookMenu();
  void showRecentActions(int index, freeink::ui::Rect anchor = {});
  // A held File row (hold Select, or a touch hold): Pin or Unpin, Rename (files), Delete, as in the file browser.
  void showFolderActions(int index);
  void folderChanged();
  static const char* missingFileText();
  void showRecentRemovalConfirmation(RecentBook book);
  // Touch: a held cover's menu hangs from the cover (rule 7), not from the whole card.
  void onRowLongPress(int index) override;
  unsigned long touchHoldMs() const override;
  void onRowAction(const freeink::ui::ActionEvent& event) override;
  freeink::ui::Rect heldCover{};
  freeink::ui::Rect contextCover{};
  freeink::ui::Rect recentCoverAt(int index);
  void drawContextBackground(freeink::ui::Rect menu);
  std::array<freeink::ui::Rect, 40> contextTextLines{};
  size_t contextTextLineCount = 0;
  void rememberContextLine(int font, int x, int y, const char* text,
                           EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void rememberContextStatGroup(size_t first, int x, int barY, int width);
  std::optional<RecentBook> pendingRecentRemoval;
  bool recentRemovalFailed = false;
  bool touchRowMovesRing() const override { return activeTabId == Tab::RECENT; }
  bool listFramed() const override { return activeTabId != Tab::RECENT; }  // the card is no list
  bool fileList() const override { return activeTabId == Tab::FOLDER; }
  bool canSearchFiles() const override { return tenorchrome::kTouchShell && activeTabId == Tab::FOLDER; }
  const char* fileSearchText() const override { return folderSearchQuery.c_str(); }
  void openFileSearch() override;
  bool rowOpens(int row) const override;
  bool rowStartsGroup(int row) const override;
  bool supportsFavorites() const override { return activeTabId != Tab::STATS; }
  std::string favoriteKey(int row) const override;
  bool buttonHoldOpensRowMenu(int row) const override;
  int focusFavorite(const std::string& key) override;
  void favoritesChanged() override;
  bool toggleFavorite(int row) override;
  bool mucQuaNhieu = false;  // the card root holds more names than the heap allows: nothing is listed
  bool favoriteFileMissing = false;
  std::optional<StrId> statsResetTip;
  const char* homeTipText();
  const char* homeInfoText();
  tenorchrome::X3ScreenLayout homeScreenLayout();
  const char* favoriteHintText() override;
  uint8_t statsPage = 0;
  // The enlarged stats tab starts with a row that turns statsPage in place.
  bool statsTurnRow(const int row) const { return statsRowsEnlarged && row == 0; }
  bool statsRowsEnlarged = false;
  // The larger text tiers split the panel into 2 pages behind a "Next page" row; the touch shell
  // scrolls the whole page instead.
  bool statsPaged() const;
  int statsPanelHeight() const;
  void drawStatsPanel(int top);
  // Touch: the panel and the rows scroll as one page between the status strip and the dynamic bar
  // (founder 06/10). The page's place, in row steps, is the Stats tab's own viewport memory
  // (tabNavs[STATS].top): a tab change keeps it, the navigation memory carries it over a screen opened
  // from Stats, a row opened from another tab drops it (forgetOtherTabs). The rows lay out from their
  // first in view in statsRows, the nav the list protocol sees on this tab (activeNav).
  bool statsScrolls() const { return tenorchrome::kTouchShell && activeTabId == Tab::STATS; }
  freeink::ui::ListNav statsRows;
  struct StatsView {
    int16_t top = 0, bottom = 0;  // the page's view on screen
    int16_t offset = 0;           // px of the page above the view
    int16_t length = 0;           // px of the whole page, the view's height when it all fits
    int16_t maxStep = 0;          // last step that still moves the page
  };
  StatsView statsView;  // render-owned; input reads maxStep under RenderLock
  freeink::ui::ListNav& activeNav() override;
  RowFrameStyle rowFrameStyle() const override;
  void buildStatsPage(UiScreen& screen);
  void drawStatsEdges();
  void confirmStatsReset(bool all);
  bool giuNutDiDong(int direction) override;
  // Header band, plus the cover tile on the Recent tab.
  void drawChrome() override;
  void drawFooter() override;

  int tabBarTop() const;
  int preferredTabBarHeight() const override;
  int coverTileTop() const;
  void selectTab(Tab tab);
  bool openLastFolder();
  std::string lastFolder() const;
  bool lastFolderAt(int row) const;
  bool lastFolderRow = false;  // mucTheNho[0] is the last folder of File
  bool restoringNavigation = false;
  void rebuildRows(bool readFolder = true);
  bool buildSettingsGroups(UiScreen& screen);
  static void settingsRow(void* context, uint16_t index, freeink::ui::ListItem& item);

  // Rows of the active tab. Rebuilt when the tab changes, never per repaint.
  std::vector<freeink::ui::ListItem> rowItems;
  std::vector<std::string> rowLabels;
  std::vector<std::string> favoriteKeys;
  std::vector<std::string> favoriteValues;
  std::vector<int> settingsGroups;
  homesettings::Order settingsOrder;
  std::string settingsTransferLabel;
  Tab activeTabId = Tab::RECENT;

  // Noi dung goc the nho, cho the Folder. Thu muc mang dau '/' o cuoi.
  std::vector<std::string> mucTheNho;
  std::string folderSearchQuery;
  filesearch::IndexBuffer folderRowIndices;
  static void searchRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);
  char searchRowName[512]{}, searchRowFolder[512]{};
  bool folderRecursiveSearch = false;
  bool folderSearchIncomplete = false;
  bool folderSearchCancelled = false;
  std::unique_ptr<filesearch::Scanner> folderSearchScanner;
  std::unique_ptr<filesearch::HalStorageAdapter> folderSearchStorage;
  std::unique_ptr<filesearch::HalScanHooks> folderSearchHooks;
  mutable std::string folderSearchEntry;
  const std::string& folderEntry(int row) const;
  std::string folderFullPath(int row) const;
  void startFolderRecursiveSearch();
  void tickFolderRecursiveSearch();
  void stopFolderRecursiveSearch();
  void docGocTheNho();

  std::vector<RecentBook> recentBooks;
  bool recentsLoaded = false;
  const HomeMenuItem initialMenuItem;
  bool cleanInitialRefresh;
  // Home took over from a book: its first frame drives the cover again (coverRedriveDue).
  bool leftBook;
  bool coverRedriveDue();
  HomeRefresh homeRefresh;
  HomeRecentRect refreshBody();
  void restoreHomeGhost(const char* reason);
  void handOffHomeGhost();
  void tickHomeGhost();

  // Cover tile snapshot, so a repaint does not decode the cover again. Only the
  // tile region is kept, not the whole framebuffer.
  bool coverRendered = false;
  bool coverBufferStored = false;
  uint8_t* coverBuffer = nullptr;
  HalMemory::PsramBuffer touchCoverBuffer;
  size_t coverBufferSize = 0;
  uint8_t coverBufferUiSize = 0;
  uint8_t coverBufferStyle = 0;
  int coverRectX = 0, coverRectY = 0, coverRectW = 0, coverRectH = 0;
  // The tenor card keeps a second region, its text block, so the blank sides of the cover are not
  // held in RAM. Zero height when the snapshot is a single region.
  int textRectX = 0, textRectY = 0, textRectW = 0, textRectH = 0;
  // Recent book the snapshot shows, so stepping to another book rebuilds the card.
  int coverBufferBook = -1;
  bool storeCoverBuffer();
  bool restoreCoverBuffer();
  void freeCoverBuffer();
  // The same two regions kept on the card next to the book's cover thumbnail, so stepping back to a
  // book shown on an earlier visit reads one file instead of decoding the cover and laying out the
  // text again (about 250 to 430 ms on the X3). Written after the frame that built them.
  std::string cardFilePending;
  uint32_t cardFileCoverKey = 0, cardFileKey = 0;
  // The snapshot is also on the card (written, or read whole), so it need not stay in RAM.
  bool cardOnCard = false;
  uint8_t cardFileThumb = 0;
  int16_t cardFileCover = 0;
  // What a card file still shows: nothing, the cover alone (the text changed), or the whole card.
  enum class CardFile : uint8_t { None, Cover, Whole };
  CardFile loadCardFile(const std::string& path, uint32_t coverKey, uint32_t key, const std::string& thumbPath);
  void saveCardFile();
  // A book whose card thumbnail is still missing: the reader writes it from the cover page or as
  // it closes, and a book read to the power key each time never took either route. The card asks
  // for it here (render task) and an idle pass writes it (main task), once per book, height and boot.
  std::atomic<int8_t> thumbWanted{-1};
  std::atomic<uint32_t> thumbWantedAtMs{0};
  int cardThumbHeight = HOME_CARD_COVER_H;
  int cardThumbWidth = HOME_CARD_COVER_W;
  struct ThumbRequest { int book = -1, width = 0, height = 0; };
  std::array<ThumbRequest, 4> touchThumbs{};
  int touchThumbCount = 0;
  bool thumbBatchWrote = false;
  void queueTouchThumb(int index, int width, int height);
  void wantThumb(int index);
  void writeMissingThumb(int index);
  // The wake's first frame is up; the main task writes state.json (V5: never the render task,
  // which raced the sleep path's own write).
  std::atomic<bool> wakeStatePending{false};

  // The tenor Recent tab shows one book at a time: ring position N shows recentBooks[N - 1], and
  // the front buttons walk the ring, so they step through the books.
  static constexpr size_t RECENT_LIMIT = 7;
  int shownRecent() const;
  // Saved quote each book's card shows during this visit, picked once per book (bit set in
  // cardQuotesPicked) and again after the Quotes screen changed the store; 0 when the book has
  // none and the card shows the page excerpt instead.
  uint64_t cardQuotes[RECENT_LIMIT] = {};
  uint8_t cardQuotesPicked = 0;
  std::string cardExcerpt(int index, bool& quoted);
  // Reading stats of the book on the card, read and formatted once per card build and drawn on
  // every paint (a few lines of flash fonts), so the cached regions stay the cover and the text.
  struct CardStats {
    uint8_t rows = 0;
    uint8_t percent = 0;
    bool recorded = false;
    std::string values[HOME_STAT_COUNT];
  };
  CardStats cardStats;
  // Each card's reading record, read from its file once per visit: only the open book's record
  // changes while Home is up, and that one is served from RAM by the store.
  BookReadingRecord cardRecords[RECENT_LIMIT];
  uint8_t cardRecordsRead = 0, cardRecordsFound = 0;
  struct CoverSize { int width = 0, height = 0; };
  CoverSize cardCoverSizes[RECENT_LIMIT];
  uint8_t cardCoverSizesRead = 0;
  CoverSize cardCoverSize(int index);
  void loadCardStats(int index);
  int drawCardStats(const HomeCardLayout& card, HomeStatsBounds* bounds = nullptr);
  void drawRecentCard();
  bool homeInkEnabled() const;
  bool homeGrayWanted() const;
  std::string touchCoverPath(const std::string& cover, int width, int height) const;
  // The cover frame of a book with no cover. 1 bit (every Home frame on the Recent tab): a white cover
  // with the book's name and author, drawn straight into the frame, nothing cached. 4 levels: the picture
  // of components/CoverHolder.h; a frame whose picture is not cached yet is drawn plain and asks for it,
  // an idle pass on the main task writes it.
  std::string holderPath(int width, int height) const;
  // True for a book with no cover of its own, the only book whose frame takes the placeholder.
  bool coverless(int index) const;
  bool drawHolder(int book, bool holder, int x, int y, int width, int height);
  void drawNameHolder(int book, int x, int y, int width, int height);
  void wantHolder(int width, int height);
  bool tickHolders();
  bool writeHolder(int width, int height);
  struct HolderRequest { int16_t width = 0, height = 0; };
  std::array<HolderRequest, 4> holderRequests{};
  std::atomic<int> holderCount{0};
  std::atomic<uint32_t> holderAtMs{0};
  bool showHomeGray();
  void clearHomeGray();
  bool homeGrayOnGlass = false;
  bool homeGraySelected = false;
  struct InkSlot { HomeRecentRect rect; int book = -1; };
  std::array<InkSlot, 4> inkSlots{};
  int inkSlotCount = 0;
  // Idle work (X3): the sharp cover of a progressive JPEG on each tenor/ink slot shown, made while
  // Home stands still. The render task fills the queue, the main task runs it; both under RenderLock.
  idlework::Queue<> idleQueue;
  uint32_t idleQuietSince = 0;  // the last key or frame
  uint32_t idleRetryAt = 0;         // after a heap floor refusal
  uint32_t idleBackoffMs = 30000;   // doubles on each refusal, up to 10 minutes
  uint8_t idleOutcome = 0;          // coverink::Outcome of the last step
  const char* idleWhy = "";        // why it ended so (the probe build logs it)
  void queueIdleCovers();
  void idleStep();
  void idleRelease();
  static idlework::Step coverStep(const idlework::Job& job, const idlework::Stop& stop);
  HomeRecentLayout recentLayout();
  void drawTouchRecent(const HomeRecentLayout& layout);
  void drawTouchCover(const HomeRecentRect& rect, int book);
  void drawOtherBookRow(int shown, int ruleY, int rowY, const std::vector<std::string>& titleLines);
  void drawOtherBookArrows(int shown, int rowY);
  const char* habitSuggestion() const;
  void loadRecentBooks();
};
