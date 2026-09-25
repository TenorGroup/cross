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
#include "ReaderToolbarUi.h"
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
  // Builds them while the cover page decodes the cover, when the book opens on it.
  std::unique_ptr<ImageBlock::ThumbHook> coverThumbs;  // a CoverThumbCapture (EpubReaderActivity.cpp)
  void writePendingThumbs();
  unsigned long lastRenderCompleteMs = 0;
  bool bookmarkRemoved = false;
  // The bookmark popup slot shows the tilt toggle's new state instead.
  bool tiltMessage = false;
  std::vector<BookmarkEntry> cachedBookmarks;
  // Anchors of this book's saved quotes, 12 bytes each and capped by the store, read
  // once per book open. Highlights are drawn from these, so no quote text is resident.
  std::vector<QuoteAnchor> quoteAnchors;
  // Name of the quote whose page the reader is moving to so the selector can reopen it there,
  // empty when no reselection waits. Written by the main loop under the render lock, and
  // cleared by renderBook when it ends on anything but a page, and by the next button press
  // before that page is shown.
  std::string pendingQuoteEdit;
  // Set by renderBook once a page is on the panel while a reselection waits; the main loop
  // then opens the selector over that page.
  std::atomic<bool> quoteEditPageShown{false};
  bool recentsEntryRemoved = false;
  unsigned long bookmarkMessageTime = 0UL;
  bool pendingReadFolderMove = false;

  // Toolbar reader menu (SETTINGS.readerMenuStyle == READER_MENU_TOOLBAR): drawn
  // over the page instead of pushing the full-screen list menu. Select opens the
  // Toolbar; its tools open the Contents/Text/More bottom-sheet panels.
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::None;
  int focusedTool = 0;  // toolbar tool focus: 0=Contents, 1=Text, 2=More
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
  static constexpr unsigned long RADIO_RELEASE_TIMEOUT_MS = 3000;
  bool releaseRadioForBuild();
  // A book opened on its chapter list builds its TOC and chapter sizes here (Epub::indexSome), one
  // step per quiet pass once the page is up and no parser is alive. A key contact stops a step
  // within milliseconds; its work is redone on a later quiet pass. A failed step waits
  // INDEX_RETRY_MS; after INDEX_MAX_FAILURES the book stays on its chapter list for this visit
  // and the radio is let go.
  static constexpr unsigned long INDEX_QUIET_MS = 1500;
  static constexpr unsigned long INDEX_RETRY_MS = 10000;
  static constexpr uint8_t INDEX_MAX_FAILURES = 3;
  unsigned long indexRetryAtMs = 0;
  uint8_t indexFailures = 0;
  bool indexStepDue() const;
  void runIndexStep();
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
  int lookAheadPage = -1;
  // Last pass that saw the radio's start in flight. The window above counts from here too: the
  // radio starts right after a book's first page, and its start can outlast the window.
  unsigned long radioSettledMs = 0;
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15;
  static constexpr int BUILD_POPUP_PAGE_THRESHOLD = 20;
  static constexpr size_t BUILD_POPUP_BYTE_THRESHOLD = 96 * 1024;
  static constexpr unsigned long BUILD_POPUP_DEADLINE_MS = 1000;
  bool buildPopupPending = false;
  void showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh);
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
  bool docCoChuMotNac(int huong) override;
  // Live section position, or the values cached before a child screen
  // released the section.
  ChapterPosition chapterPosition() const;
  int bookPercentFor(const ChapterPosition& position) const;
  void openReaderMenu();
  // Toolbar reader menu (see Overlay above).
  bool usesToolbarMenu() const;
  void openOverlay(Overlay target);
  void closeOverlayToPage();
  void discardOverlayPage();
  void handleOverlayInput();
  void renderOverlay();
  std::string currentChapterTitle() const;
  // Text panel rows (font, size, line spacing, alignment, focus reading).
  std::string textRowName(int row) const;
  std::string textRowValue(int row) const;
  void showTextRowPopup(int row);
  // Persist + re-paginate + re-render under the open panel (live preview).
  void applyTextSettingLive();
  void paintOverlayPopup();
  // Persist the reader text settings, (re)load the selected SD font, and
  // re-paginate the current chapter so changes apply without re-opening the book.
  void applyReaderTextSettings();
  // More panel rows.
  void buildMoreActions();
  std::string moreRowName(int row) const;
  std::string moreRowValue(int row) const;
  void activateMoreRow(int row);
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
  bool manualPageTurnReady() const override;
  bool pageAwaitsLayout() const override;
  bool loadBook() override;
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

  ScreenshotInfo getScreenshotInfo() const override;
  CrossPointPosition getCurrentPosition() const;
};
