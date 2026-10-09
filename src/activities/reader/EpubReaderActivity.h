#pragma once

#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/PageLink.h>
#include <Epub/Section.h>
#include <Epub/blocks/ImageBlock.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "BookmarkEntry.h"
#include "ChapterPosition.h"
#include "EpubReaderMenuActivity.h"
#include "ProgressMapper.h"
#include "QuoteStore.h"
#include "ReaderActivity.h"
#include "ReaderFontChon.h"
#include "ReaderToolbarUi.h"
#include "ReaderTextRelayout.h"
#include "components/OptionPopup.h"

class EpubReaderActivity final : public ReaderActivity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  // Khoang muc TOC nam trong spine dang doc, tinh MOT lan cho moi spine. Khong co no thi moi lan
  // giu nut phai quet lai bang muc luc (getTocItem doc cache metadata tung muc) — do la chi phi
  // N lan goi cho moi nhip giu. Xoa cache trong loadBook() khi mo sach khac.
  int tocSpineCached = -1;
  int tocDauTrongSpine = -1;
  int tocCuoiTrongSpine = -1;
  int tocTruocTrongSpine = -1;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  std::string pendingAnchor;
  struct ChapterHoldOrigin {
    int spineIndex = -1;
    int pageNumber = 0;
    std::string pendingAnchor;
  };
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  // Set by the main loop on a page turn, consumed under the render lock.
  std::atomic<bool> deferredClearPending{false};
  void takePendingDeferredClear();
  std::optional<uint32_t> currentPageVisibleOffset;
  std::optional<uint32_t> pendingOffsetJump;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  // Atomic: the render task reads it to drop a paint nobody will see (nextScreenWaiting).
  std::atomic<int8_t> pendingManualTurn{0};
  // Why the page being painted is about to be replaced (a queued turn, a chapter jump waiting for
  // the render lock, the reader closing), or nullptr. Before anything reaches the panel any of these
  // drops the paint. Once the page is readable only leaving it (Back, a held chapter jump) cuts the
  // gray pass: for queued turns that ended paints early, and presses that used to merge into one
  // repaint each got their own (X3 r43, 27 refreshes where 19 were).
  const char* nextScreenWaiting() const;
  const char* pageBeingLeft() const;
  std::atomic<bool> jumpWaiting{false};
  // Set by renderContents when it returned before the panel was touched. The loop asks for a
  // repaint if the presses that caused it cancelled out, or the panel would keep the old page.
  std::atomic<bool> paintDropped{false};
  // Set by a paint that left its progress write to the next paint, an idle pass or the exit.
  std::atomic<bool> progressSaveDeferred{false};
  // The last owed write failed on the card; the idle pass stops retrying it (RenderLock guards it).
  bool progressSaveFailed = false;
  // The framebuffer holds exactly the page frame on the panel: set when renderBook ends on a
  // page, cleared when a paint begins and when another screen covers the reader. Under RenderLock.
  std::atomic<bool> pageFrameShown{false};
  class BackgroundBuildFrameGuard {
   public:
    explicit BackgroundBuildFrameGuard(EpubReaderActivity& activity);
    ~BackgroundBuildFrameGuard();

    BackgroundBuildFrameGuard(const BackgroundBuildFrameGuard&) = delete;
    BackgroundBuildFrameGuard& operator=(const BackgroundBuildFrameGuard&) = delete;

   private:
    EpubReaderActivity& activity;
    uint32_t loanCount;
  };
  void invalidatePageFrameAfterLoan(uint32_t loanCount);
  // That page leaves the panel as it is under a fast differential refresh that only changes the
  // status bar (see renderContents), and the charging state its status bar shows.
  bool pageFrameKeepsUnderFast = false;
  bool pageFrameUsb = false;
  // USB power came or went; the main loop redraws the status bar alone on a quiet pass.
  bool statusBarStale = false;
  void repaintStatusBarAlone();
  // The page turner's link note changed: the main loop redraws the status bar alone as for USB.
  void redrawLinkNote() override;
  void saveProgressIfMoved();
#ifdef TENOR_TURN_TRACE
  TurnTrace pendingManualTurnTrace;
  uint32_t paintTraceSequence = 0;
  unsigned long paintTraceStarted = 0;
  bool readablePaintTraced = false;
  void traceBuildTickBegin(const char* source) const;
  void traceBuildTickEnd(const char* source) const;
  void tracePaint(const char* phase, const char* kind) const;
  void traceReadablePaint(const char* kind);
#endif
  std::optional<ChapterHoldOrigin> chapterHoldPrevOrigin;
  std::optional<ChapterHoldOrigin> chapterHoldNextOrigin;
  // Turbo hold: sau nắc chương đầu (long-press), nút vẫn giữ thì tiếp tục nắc
  // theo nhịp cố định tới khi thả. Nguồn nhớ từ lần nắc gần nhất.
  int8_t turboHoldDirection = 0;
  unsigned long turboNextJumpMs = 0UL;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool pendingSyncSaveError = false;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  bool skipNextButtonCheck = false;
  bool automaticPageTurnActive = false;
  bool showBookmarkMessage = false;
  bool showDictionaryMessage = false;
  // "Indexing": a screen that needs the TOC or the chapter sizes was asked for before the book's
  // index was whole (waitsForIndex).
  bool showIndexingMessage = false;
  unsigned long indexingMessageTime = 0UL;
  bool waitsForIndex();
  unsigned long dictionaryMessageTime = 0UL;
  bool currentPageBookmarked = false;
  int idlePrewarmSpine = -1;
  int idlePrewarmPage = -1;
  // The GAN DAY card's cover thumbnails are read only by Home. loadBook() records which heights
  // are missing (the card's own, then the theme's); onExit() writes them as the reader closes.
  int pendingThumbHeights[2] = {};
  uint8_t pendingThumbCount = 0;
  // The thumbnails were owed at the open: once its first frame is up, the book tells Home where its
  // cover is (coverref), should they still be owed then.
  bool coverRefPending = false;
  // Builds them while the cover page decodes the cover, when the book opens on it.
  std::unique_ptr<ImageBlock::ThumbHook> coverThumbs;  // a CoverThumbCapture (EpubReaderActivity.cpp)
  void writePendingThumbs();
  unsigned long lastRenderCompleteMs = 0;
  bool bookmarkRemoved = false;
  // The bookmark popup slot shows the tilt toggle's new state instead.
  bool tiltMessage = false;
  bool bleConnectMessage = false;
  std::vector<BookmarkEntry> cachedBookmarks;
  // Anchors of this book's saved quotes, 12 bytes each and capped by the store, read
  // once per book open. Highlights are drawn from these, so no quote text is resident.
  std::vector<QuoteAnchor> quoteAnchors;
  // Name of the quote whose page the reader is moving to so the selector can reopen it there,
  // empty when no reselection waits. Written by the main loop under the render lock, and
  // cleared by renderBook when it ends on anything but a page, and by the next button press
  // before that page is shown.
  std::string pendingQuoteEdit;
  // Shortcut asked by a quick action (requestShortcut), taken at the top of the next loop pass.
  ReaderShortcut pendingShortcut = ReaderShortcut::None;
  // Set by renderBook once a page is on the panel while a reselection waits; the main loop
  // then opens the selector over that page.
  std::atomic<bool> quoteEditPageShown{false};
  bool recentsEntryRemoved = false;
  unsigned long bookmarkMessageTime = 0UL;
  bool pendingReadFolderMove = false;

  // Toolbar reader menu (SETTINGS.readerMenuStyle == READER_MENU_TOOLBAR): drawn
  // over the page instead of pushing the full-screen list menu. Select opens the
  // Toolbar; its tools open the Contents/Text/More bottom-sheet panels.
  // Favorites: the X4 Pro's 4th tool (founder 06/10).
  enum class Overlay { None, Toolbar, Contents, Text, More, Favorites };
  Overlay overlay = Overlay::None;
  // The tool in focus, by its place on the bar (readermenu::Tool). The menu opens on Contents, as it always has.
  int focusedTool = static_cast<int>(readermenu::Tool::CONTENTS);
  int panelIndex = 0;   // selected row within the active panel
  // Panel list navigation: a tap steps one row, a hold jumps PANEL_HOLD_STEP rows in one go
  // (a contents list runs to hundreds of chapters). One jump per hold, not a repeat -- every
  // step repaints the panel, so repeating is bounded by the e-ink refresh anyway and reads as
  // sluggish. True once a hold has jumped, so the release that ends it is swallowed.
  static constexpr unsigned long PANEL_HOLD_MS = 1500;
  static constexpr int PANEL_HOLD_STEP = 10;
  bool panelHoldJumped = false;
  // Whether the panel draws its cursor row. Button boards always do; touch
  // boards only once a button has moved it, so a tapped row is not left inverted.
  bool panelCursorShown = false;
  // FreeInkUI chrome + tap targets for the overlay; created when it opens,
  // released when it closes.
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  // Modal option picker over the panel (same component the Settings screens
  // use), for enum rows: font size / line spacing / alignment / orientation /
  // auto page turn. Toggle rows stay one-tap toggles, as in Settings.
  OptionPopup overlayPopup;
  // True while a clean-page snapshot (renderer.storeBwBuffer) backs the open
  // overlay, letting panel->toolbar steps restore the page without a full
  // re-render. Discarded on close / whenever the page under the overlay changes.
  bool overlayPageStored = false;
  bool bwUnderSheet = false;
  // True while a deferred overlay chrome refresh (pushOverlayRefresh) may still
  // be running on the panel. settleOverlayRefresh() must run before the
  // framebuffer is touched or another differential refresh is pushed.
  bool overlayRefreshPending = false;
  // Text panel changes are applied to the page at once but written to settings.json when the
  // panel closes (flushTextSettings), not at every step: a card write is 200-400 ms on the X3.
  bool textSettingsDirty = false;
  std::atomic<uint8_t> textCloseFrame{0};
  void markClosedTextFrameUpLocked();
  void flushTextSettingsLocked();
  void flushTextSettings();
  // The Font row opens a second level inside the Text panel: the same sheet lists the families
  // (the 2 built in, then the card's), the page above is the preview. Back returns to the rows.
  enum class TextDepth : uint8_t { Rows, Fonts, Spacing, PointSize, Pick };
  TextDepth textDepth = TextDepth::Rows;
  // A second level (the fonts, a value list) keeps the frame of the sheet it opened from: its rows.
  int levelSheetRows = 0;
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
  // Buttons (founder 06/10): a row with several values opens them in the same sheet, as the Font row opens
  // the fonts (TextDepth::Pick). The front buttons move, Select keeps the value, Back drops it.
  // source: a Text row id (1 size, 2 line spacing, 3 alignment, 4 drop cap) or PICK_MORE + a More action.
  static constexpr int PICK_MORE = 100;
  struct Pick {
    int source = -1;
    int origin = 0;     // the row that opened it, where Select and Back return the cursor
    int inUse = 0;      // the value in use: bold with the tick, the cursor's first place
    std::string title;
    std::vector<std::string> labels;
  };
  Pick pick;
  void openPick(int source, std::string title);
  void leavePick(bool keep);
  // The value at `place` of the list `source`. True when the page is laid out again: its repaint brings
  // the sheet back.
  bool applyPick(int source, int place);
#endif
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  int spacingDraftPermille = 500;
  bool spacingDragging = false;
  std::string pointSizeDraft;
  void enterTextDepth(TextDepth depth);
  void stepMenuPointSize(int direction);
  void applyMenuPointSize(uint8_t pointSize);
  uint8_t enteredPointSize() const;
#endif
  // Favorites: the pins (readermenu PIN_TEXT | text row id, or an Action) and the rows shown, the pins this
  // book has. A hold on a Text, More or Favorites row pins it or takes it off (touch: a long press; buttons:
  // Select held readermenu::GIU_GHIM_MS).
  std::vector<uint8_t> pins;
  std::vector<uint8_t> favoriteRows;
  void loadPins();
  void togglePin(uint8_t pin);
  uint8_t pinOfRow(int row) const;
  std::string favoriteRowName(int row) const;
  std::string favoriteRowValue(int row) const;
  void activateFavoriteRow(int row);
  void enterFontLevel();
  void leaveFontLevel();
  void chooseFontFamily(int index);
  // The panel has closed (or the reader is leaving): save what it changed, drop its second level.
  void panelClosed(bool leaving = false, bool frameUp = false);
  void panelClosedLocked(bool leaving, bool frameUp);
  void pushOverlayRefresh();
  void settleOverlayRefresh();
  void redrawSheetLocked();
  void setReaderStatusBarMode(int mode);
  int autoTurnOption = 0;  // current auto page-turn rate index (More panel)
  std::vector<EpubReaderMenuActivity::MenuItem> moreItems;

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  std::vector<PageLink> currentPageLinks;
  int currentPageLinkMarginLeft = 0;
  int currentPageLinkMarginTop = 0;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;
  // The back-stack outlives the reader (sleep, home) in links.bin so Back
  // still returns to where a followed link was tapped. The card is touched after a frame, never
  // before: open only reads the file and the first frame's tick removes it (linkStackOnCard), the
  // exit writes it after the next screen's first frame (ActivityManager::deferWrite) or at once
  // when the device sleeps. Preview neither reads nor removes it.
  void saveLinkStack() const;
  void loadLinkStack();
  bool linkStackOnCard = false;

  uint16_t buildViewportWidth = 0;
  uint16_t buildViewportHeight = 0;
  bool partialRebuildStartFailed = false;

  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;

  static constexpr int BUILD_PAGES_PER_CHUNK = 8;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024;
  // Conservative pre-parser admission budget; ticks use the smaller resident-build budget.
  static constexpr size_t BACKGROUND_BUILD_START_MIN_FREE_HEAP = 64 * 1024;
  static constexpr size_t BACKGROUND_BUILD_START_MIN_MAX_ALLOC = 32 * 1024;
  bool deferBackgroundBuildForBle() const;
  bool backgroundBuildStartHeapGate();
  // Reads the section: requires the render lock. Heap admission is checked separately by the tick.
  bool backgroundBuildWanted() const;
  bool buildTickHeapGate();
  bool backgroundBuildCanTick();
  // Caller owns RenderLock. Heap-pressure suspension resumes only for an explicit target.
  void suspendBackgroundBuild();
  bool backgroundBuildSuspended = false;
  // Heap the parser handed back at the last park. The resume gate has to cover this on top
  // of the tick budget, or the resume buys one page and pays for the next park.
  size_t parkedParserFootprint = 0;
  // A failed speculative tick stays disabled until this reader visit ends.
  bool backgroundBuildFailed = false;
  // Set while the radio is stopped so a starved section build can finish. The reader
  // asks for a restart once the page is on screen.
  bool radioReleasedForBuild = false;
  // Set once a paint has shed the font caches for a starved build: a second starve in the same
  // paint stops the radio instead.
  bool fontsShedForBuild = false;
  bool releaseHeapForBuild();
  bool startBuildFreeingHeap(const ReaderRenderSpec& spec, const std::function<void()>& popupFn = nullptr);
  // A book opened on its chapter list builds its TOC and chapter sizes here (Epub::indexSome), one
  // step per quiet pass once the page is up and no parser is alive. A key contact stops a step
  // within milliseconds; its work is redone on a later quiet pass. A failed step waits
  // INDEX_RETRY_MS; after INDEX_MAX_FAILURES the book stays on its chapter list for this visit
  // and the radio is let go.
  static constexpr unsigned long INDEX_QUIET_MS = 1500;
  static constexpr unsigned long INDEX_RETRY_MS = 10000;
  static constexpr uint8_t INDEX_MAX_FAILURES = 3;
  // Steps stopped by a key one after another before the index is given up for the visit: a step
  // loses at most a chunk now, but a reader who never pauses long enough still gets the radio back.
  static constexpr uint8_t INDEX_MAX_STOPS = 40;
  unsigned long indexRetryAtMs = 0;
  uint8_t indexFailures = 0;
  uint8_t indexStops = 0;
  bool indexStepDue() const;
  void runIndexStep();
  void dropSectionsLaidOutWithoutToc();
  void showMemoryError();
  void forgetPendingJump();
  void stayAfterStarvedJump();
  bool buildHeapPaused = false;
  static constexpr size_t RENDER_MIN_FREE_HEAP = 24 * 1024;
  static constexpr int BUILD_WINDOW_AHEAD = 5;
  // Quiet-pass look-ahead while the radio keeps the parser parked: not before the page
  // has settled, and not so late that the loop already runs down-clocked.
  static constexpr int BUILD_WINDOW_QUIET_MS = 400;
  static constexpr int BUILD_WINDOW_LATEST_MS = 1200;
  // Bound on one look-ahead: the next page took 780 ms on the X3, a pathological one stops here.
  static constexpr int BUILD_WINDOW_MAX_MS = 1500;
  // Pages the look-ahead keeps laid out past the one on screen. With one, the second page of a
  // quick burst of two turns was laid out inside the paint: 432 ms of the 1.2 s that burst took
  // (X3 r29). Once ahead, each turn lays out one page, as with one.
  static constexpr int LOOK_AHEAD_PAGES = 2;
  int lookAheadPage = -1;
  // The next chapter's first pages, laid out on a quiet pass on the last page of a whole chapter,
  // once per chapter and visit (prepareNextChapter).
  static constexpr unsigned long NEXT_CHAPTER_QUIET_MS = 1500;
  int nextChapterPrepared = -1;
  bool nextChapterDue(bool inputThisPass);
  void prepareNextChapter();
  // Last pass that saw the radio's start in flight. The window above counts from here too: the
  // radio starts right after a book's first page, and its start can outlast the window.
  unsigned long radioSettledMs = 0;
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15;
  static constexpr int BUILD_POPUP_PAGE_THRESHOLD = 20;
  static constexpr size_t BUILD_POPUP_BYTE_THRESHOLD = 96 * 1024;
  static constexpr unsigned long BUILD_POPUP_DEADLINE_MS = 1000;
  bool buildPopupPending = false;
  void showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh);
  // The build popup's refresh runs beside the layout that follows it; settleBuildPopup() waits it
  // out before anything else is drawn, and renderBook() never returns with it running.
  bool buildPopupRefreshing = false;
  void settleBuildPopup();
  bool applyDeferredReposition();
  void clearDeferredReposition();
  void rememberCurrentContentOffset();
  int logicalTocIndexForPosition(const ChapterHoldOrigin& origin, int huong);
  void rememberChapterHoldOrigin(int huong);
  // Doi dung mot muc muc luc theo huong +1/-1, dung lai dung duong ma man Chon chuong
  // van dung (spine + anchor). Tra ve false khi khong co muc luc hoac da o dau/cuoi.
  // `khoaDaGiu` = nguoi goi dang giu san khoa ve; lay them mot lan nua la khoa long nhau.
  bool nhayChuongMotBac(int huong, std::optional<int> logicalOrigin = std::nullopt, bool khoaDaGiu = false);
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action, const MenuResult& menu);
  // Vo hieu section de dan lai trang voi chu moi, giu dung doan dang doc. GOI DUOI RenderLock.
  void danLaiTrang();
  // Text settings changed: the page on screen is laid out alone from the nearest resume point
  // (Section::previewPage) and the chapter is laid out again behind it between key presses
  // (catchUpTick). One request is kept, the latest: a change drops the layout of the one before.
  bool xemTruoc = false;              // section is null and the page shown is a preview
  uint32_t xemTruocDich = 0;          // the old page's first character: the same for every change
  std::unique_ptr<Section> catchUp;   // the chapter laid out under the latest settings
  int8_t xemTruocLat = 0;             // a turn asked from the preview, applied once the chapter lands
  bool xemTruocTrenMan = false;       // the chapter landed while the preview stayed on screen
  unsigned long xemTruocInputMs = 0;  // catch-up waits for a pause in the presses
  static constexpr unsigned long CATCH_UP_QUIET_MS = TextRelayoutQuiet::kQuietMs;
  static constexpr uint8_t CATCH_UP_MAX_FAILS = 3;
  uint8_t catchUpFails = 0;
  void dropCatchUp();
  bool catchUpCanTick() const;
  void catchUpTick(bool inputThisPass);
  bool renderPreview(int marginTop, int marginRight, int marginBottom, int marginLeft);
  bool docCoChuMotNac(int huong) override;
  // Live section position, or the values cached before a child screen
  // released the section.
  ChapterPosition chapterPosition() const;
  int bookPercentFor(const ChapterPosition& position) const;
  void openReaderMenu();
  // Toolbar reader menu (see Overlay above).
  bool usesToolbarMenu() const;
  void openOverlay(Overlay target);
  void stopRadioForSheet();
  void closeOverlayToPage();
  void discardOverlayPage();
  void handleOverlayInput();
  void renderOverlay();
  std::string currentChapterTitle() const;
  // Text panel rows (font, size, line spacing, alignment, focus reading).
  std::string textRowName(int row) const;
  std::string textRowValue(int row) const;
  void showTextRowPopup(int row);
  // A Text row by its place on the Text panel. X4 Pro: Font, Size and Line spacing open their level, the others
  // step. Buttons: Font opens the fonts, an on/off row turns, the others open their values over the sheet.
  void openTextRow(int row);
  void cycleTextRow(int row);
  void chooseTextValue(int row, int place);
  // Mark for saving + re-paginate + re-render under the open panel (live preview).
  void applyTextSettingLive();
  void paintOverlayPopup();
  // Mark the reader text settings for saving, (re)load the selected SD font, and
  // re-paginate the current chapter so changes apply without re-opening the book.
  void applyReaderTextSettings();
  void applyReaderTextSettingsLocked(const char* key = nullptr);
  void invalidateTextSettingsLocked();
  // The rendered page's glyph caches can yield their heap to the overlay
  // snapshot, including the first sheet opened with unchanged settings.
  void releaseTextCachesBeforeOverlaySnapshot();
  // More panel rows.
  void buildMoreActions();
  std::string moreRowName(int row) const;
  std::string moreRowValue(int row) const;
  void activateMoreRow(int row);
  void openFootnoteSelect(bool reopenMenuOnCancel);
  // `editName` non-empty reopens that saved quote for reselection on the current page.
  void openDictionaryWordSelect(bool quotation = false, const std::string& editName = {});
  // Tools > Quotations in this book: the Quotes screen for this book only. A quote it hands
  // back for reselection is jumped to, and the selector opens once its page is drawn.
  void openBookQuotes();
  void jumpToQuoteForEdit(const std::string& name);
  bool launchKOReaderSync();
  unsigned long confirmLongPressThreshold() const;
  void toggleTiltFromReader();
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void loadCachedBookmarks();
  void addBookmark();
  void updateBookmarkFlag();

  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  readerstatus::Pace::Position pacePosition() const override;
  uint32_t paceLayoutKey() const override;
  // Invert the words of every saved quote that reaches this page, exactly the way the
  // quote selector inverts a live selection. Called on the black and white pass only.
  void drawQuoteHighlights(const Page& page, int fontId, int marginLeft, int marginTop) const;
  void applyOrientation(uint8_t orientation);
  void applyInitialOrientation() override;
  // The orientation the current layout was built for. The control center's
  // orientation tile can move SETTINGS.orientation while this reader sits on
  // the activity stack, and Pop restores it without onEnter(), so the drift has
  // to be noticed here rather than assumed away.
  uint8_t appliedOrientation = 0;

  bool externalPageTurnAllowed() const override;
  bool requestShortcut(ReaderShortcut shortcut) override;
  bool manualPageTurnReady() const override;
  bool pageAwaitsLayout() const override;
  bool loadBook() override;
  bool handleTapTip();
  bool readingPageVisible() const override;
  std::string getBookTitle() const override { return epub ? epub->getTitle() : ""; }
  std::string getBookAuthor() const override { return epub ? epub->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return epub ? epub->getThumbBmpPath() : ""; }
  void renderBook() override;
  void onEndOfBookRendered() override;

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                              bool allowFastInitialRefresh)
      : ReaderActivity("EpubReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~EpubReaderActivity() override;
  void onPause() override;
  void onExit() override;

  void loop() override;

  bool latTrangThat(bool isForward) override;
  bool skipPages(int amount) override;
  bool nhayChuongThat(int huong) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  bool skipLoopDelay() override;
  bool holdsRadio() const override;
  bool yieldForRadio() override;
  bool coversPage() const override { return overlay != Overlay::None; }

  ScreenshotInfo getScreenshotInfo() const override;
  CrossPointPosition getCurrentPosition() const;
};
