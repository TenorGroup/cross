#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include "activities/reader/ReaderPace.h"
// Swallow the arguments instead of the whole call: values a log line is the only
// consumer of (the idle prewarm timer) must still count as used under -Werror.
template <class... A>
inline void logSink(A&&...) {}
#define LOG_DBG(...) logSink(__VA_ARGS__)
#define LOG_ERR(...) logSink(__VA_ARGS__)
#define LOG_INF(...) logSink(__VA_ARGS__)
constexpr int STR_INDEXING = 1;
#define STR_PAGE_LOAD_ERROR 2
#define STR_MEMORY_ERROR 3
constexpr int STR_LOADING_POPUP = 4;
#define UI_12_FONT_ID 12
struct EpdFontFamily { static constexpr int BOLD = 1; };
int tr(int n) { return n; }
// The indexing and loading notices come through the voice of the shell (readerugly::notice); here, the plain strings.
namespace StrId { constexpr int STR_INDEXING = ::STR_INDEXING, STR_LOADING_POPUP = ::STR_LOADING_POPUP; }
namespace readerugly { inline int notice(int id) { return tr(id); } }
uint32_t clockMs = 1000;
uint32_t millis() { return clockMs; }
int popupCount = 0, buildErrors = 0;
uint32_t popupAtMs = 0;
// Cover thumbnail for the GAN DAY card. The counters let a case say WHEN the
// JPEG->BMP pass ran (while a page is read, or as the reader closes), and whether it borrowed
// the framebuffer to do it.
// `fileOnCard` stands for every height already written; `onCard` for some heights only (a book
// opened before the card's own thumbnail existed). `heights` records the order of generation.
struct ThumbState {
  int existsChecks = 0, generated = 0, loans = 0;
  bool fileOnCard = false;
  std::vector<int> onCard, heights;
  bool has(int height) const { return fileOnCard || std::find(onCard.begin(), onCard.end(), height) != onCard.end(); }
} thumbs;
int thumbHeightOf(const char* path) {
  const std::string name(path);
  const auto at = name.rfind("thumb_");
  return at == std::string::npos ? 0 : std::atoi(name.c_str() + at + 6);
}
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct RenderLock {
  struct TryTake {};
  static inline bool busy = false;
  bool owns = false;
  RenderLock() { require(!busy, "recursive lock"); busy = owns = true; }
  explicit RenderLock(TryTake) { if (!busy) busy = owns = true; }
  bool acquired() const { return owns; }
  ~RenderLock() { if (owns) busy = false; }
};
struct Heap { size_t free = 80000, largest = 60000; size_t getFreeHeap() { return free; } size_t getMaxAllocHeap() { return largest; } } ESP;
struct ReaderRenderSpec {};
struct Settings {
  bool blePageTurnerEnabled = false;
  int saves = 0; void saveToFile() { ++saves; }
  int getReaderFontId() const { return 0; }
  ReaderRenderSpec readerRenderSpec(int, int) { return {}; }
} SETTINGS;
struct StorageStub {
  bool exists(const char* path) { ++thumbs.existsChecks; return thumbs.has(thumbHeightOf(path)); }
} Storage;
struct ThemeMetrics { int homeCoverHeight = 226; };
// The tenor Recent card and the height it draws the cover at (components/HomeExcerptStyle.h).
inline constexpr int HOME_CARD_COVER_H = 450;
namespace tenorchrome {
inline bool enabledState = true;
inline bool enabled() { return enabledState; }
}  // namespace tenorchrome
class UITheme {
 public:
  static UITheme& getInstance() { static UITheme instance; return instance; }
  const ThemeMetrics& getMetrics() const { return metrics_; }

 private:
  ThemeMetrics metrics_;
};
struct PrewarmScope { void endScanAndPrewarm() {} };
// Rebuildable font caches the reader may shed for a starved build; counts each shed.
inline int fontSheds = 0;
struct FontCacheManager {
  PrewarmScope createPrewarmScope() { return {}; }
  void releaseSdFontCaches() { ++fontSheds; }
};
inline FontCacheManager fontCaches;
// Probe-only heap map; the release build compiles it away.
inline void heapMapDump(const char*) {}

namespace freeink::ble {
inline bool busyState = false;
inline bool initializingState = false;
inline bool readerStartDeferredState = false;
inline bool idleStoppedState = false;
inline int stopForIdleCalls = 0, rearmRequests = 0;
inline bool busy() { return busyState; }
inline bool initializing() { return initializingState; }
inline bool readerStartDeferred() { return readerStartDeferredState; }
inline bool idleStopped() { return idleStoppedState; }
// False models a radio that does not come down within the release timeout.
inline bool stopForIdleResult = true;
inline bool stopForIdle() { ++stopForIdleCalls; if (stopForIdleResult) idleStoppedState = true; return stopForIdleResult; }
inline void requestRearm() { ++rearmRequests; }
inline bool heldForBuildState = false;
inline void setRadioHeldForBuild(bool held) { heldForBuildState = held; }
}  // namespace freeink::ble
inline void delay(uint32_t ms) { clockMs += ms; }
// The page turner module's contract (lib/BlePageTurner) over the knobs above: the reader asks
// it whether the radio holds the heap, to release the radio for a starved build, and whether a
// start is in flight.
namespace bleturner {
enum class BuildRelease : uint8_t { NotHeld, Released, StillUp };
struct Status { bool starting = false; };
inline bool holdsHeap() {
  return SETTINGS.blePageTurnerEnabled &&
         (freeink::ble::busyState || freeink::ble::initializingState || freeink::ble::readerStartDeferredState);
}
inline Status status() { Status s; s.starting = freeink::ble::initializingState; return s; }
inline void afterPaint() { freeink::ble::requestRearm(); freeink::ble::heldForBuildState = false; }
// Waits up to 3 s for a start in flight, then stops the radio, trying again for up to 3 s.
inline BuildRelease beforeChapterBuild() {
  if (!SETTINGS.blePageTurnerEnabled || freeink::ble::idleStoppedState) return BuildRelease::NotHeld;
  const uint32_t started = clockMs;
  while (freeink::ble::initializingState && clockMs - started < 3000) delay(20);
  if (freeink::ble::initializingState) return BuildRelease::NotHeld;
  freeink::ble::heldForBuildState = true;
  const uint32_t stopStarted = clockMs;
  bool stopped = freeink::ble::stopForIdle();
  while (!stopped && clockMs - stopStarted < 3000) {
    delay(20);
    stopped = freeink::ble::stopForIdle();
  }
  return stopped ? BuildRelease::Released : BuildRelease::StillUp;
}
// The status bar's link note: no remote in these cases, the title stays.
enum class LinkNote : uint8_t { None, Connecting, Failed };
inline LinkNote linkNote() { return LinkNote::None; }
inline void acknowledgeLinkNote() {}
}  // namespace bleturner
// Thumbnails written when the last popup went up, so a case can tell a notice came first.
int popupGenerated = -1;
// The panel refreshing a frame the caller asked for without waiting (displayBufferAsync); layout
// ticks count while it runs, and nothing may draw until the wait.
inline bool panelRefreshing = false;
int blockingPopups = 0;
struct HalDisplay { enum RefreshMode { FAST_REFRESH, HALF_REFRESH }; };
struct Gui {
  void drawPopup(int, int, bool display = true) {
    ++popupCount; popupAtMs = millis(); popupGenerated = thumbs.generated;
    if (display) { require(!panelRefreshing, "a popup went up over a refresh still running"); ++blockingPopups; }
  }
} GUI;
struct PageReadState { int failures = 0, reads = 0, clears = 0, abandons = 0, errors = 0; } pageReads;
struct ReaderRenderer {
  uint32_t frameBufferLoans = 0;
  uint32_t frameBufferLoanCount() const { return frameBufferLoans; }
  operator int() const { return 0; }
  bool hasFrameBuffer() const { return true; }
  void clearScreen() { require(!panelRefreshing, "drew over a refresh still running"); }
  void displayBufferAsync(HalDisplay::RefreshMode) { panelRefreshing = true; }
  void waitRefreshComplete() { panelRefreshing = false; }
  void drawCenteredText(int, int, int message, bool, int) { if (message == STR_PAGE_LOAD_ERROR) ++pageReads.errors; }
  void displayBuffer() {}
  FontCacheManager* getFontCacheManager() { return &fontCaches; }
  // The real loan hands the framebuffer bytes to a build phase and returns it
  // WHITE, so only a caller that redraws the whole screen may take one.
  class FrameBufferLoan {
   public:
    explicit FrameBufferLoan(ReaderRenderer& renderer) { ++thumbs.loans; ++renderer.frameBufferLoans; }
  };
};
using GfxRenderer = ReaderRenderer;
struct ReaderPaceStub {
  void suspend() {}
  template <class... Args> void turned(Args&&...) {}
};
struct Section {
  int currentPage = 4, pageCount = 19, oldPages = 19, builtPages = 4;
  bool building = true, partial = true, complete = false, parked = false, canPark = false;
  bool failStart = false, failTick = false, dropAfterTick = false, dropAfterStart = false;
  bool starveUntilRadioStopped = false, starved = false;
  // Starved until the reader sheds its font caches (a remote linked, X3 02/10: 16,184 B free
  // against the 16,384 B floor).
  bool starveUntilFontsShed = false;
  bool buildStarved() const { return starved; }
  int starts = 0, ticks = 0, suspends = 0, parks = 0, resumes = 0, ticksPerPage = 0, ticksIntoPage = 0;
  int ticksWhileRefreshing = 0;
  int restoredPagesAfterStart = 0;
  uint32_t startMs = 0, tickMs = 0;
  // Heap the park gives back, and what the parser takes again when it resumes.
  // Both are knobs so a case can reproduce the device numbers around a park.
  size_t parkRestoresFree = 80000, parkRestoresLargest = 60000, parserFootprint = 0;
  struct Page {
    void render(const ReaderRenderer&, int, int, int) const {}
  };
  std::unique_ptr<Page> loadPage(int page) {
    require(RenderLock::busy, "load without render lock");
    require(page == currentPage, "read changed target");
    ++pageReads.reads;
    if (pageReads.failures > 0) { --pageReads.failures; return nullptr; }
    return std::make_unique<Page>();
  }
  void abandonBuild() { ++pageReads.abandons; building = false; }
  bool clearCache() { ++pageReads.clears; return true; }
  bool isBuilding() const { return building; }
  bool isBuildParked() const { return building && parked; }
  bool isPartial() const { return partial; }
  bool isBuildComplete() const { return complete; }
  std::optional<int> anchorPage;
  std::optional<int> findAnchor(const std::string&) const { return anchorPage; }
  std::optional<int> findAnchorDuringBuild(const std::string&) const { return anchorPage; }
  bool buildReachedVisibleTextOffset(uint32_t) const { return false; }
  // Share of the chapter laid out: 40 pages make the whole chapter here.
  int estimatedTotalPages() const { return pageCount; }
  bool laidOutTo(float share) const { return complete || builtPages >= static_cast<int>(share * 40); }
  template <class F> bool startBuild(const ReaderRenderSpec& spec, F&&) { return startBuild(spec); }
  bool startBuild(const ReaderRenderSpec&) { require(RenderLock::busy, "start without render lock"); ++starts; clockMs += startMs; if (failStart) return false; building = true; parked = false; builtPages = restoredPagesAfterStart; if (dropAfterStart) ESP.free = 29000; return true; }
  bool buildSomeMore(int n) {
    require(RenderLock::busy, "build without render lock"); ++ticks; clockMs += tickMs;
    if (panelRefreshing) ++ticksWhileRefreshing;
    if (parked) {
      parked = false; ++resumes;
      ESP.free = ESP.free > parserFootprint ? ESP.free - parserFootprint : 0;
    }
    if (failTick) return false;
    starved = (starveUntilRadioStopped && !freeink::ble::idleStoppedState) ||
              (starveUntilFontsShed && fontSheds == 0);
    if (starved) { parked = canPark; return false; }
    // The real tick yields after about 20 ms of parsing (Section::buildSomeMore), so on the X3 one
    // page takes dozens of ticks. ticksPerPage > 0 models that: a page lands every that many ticks.
    if (ticksPerPage > 0) builtPages += ++ticksIntoPage % ticksPerPage == 0 ? 1 : 0;
    else builtPages += n;
    pageCount = std::max(oldPages, builtPages);
    if (dropAfterTick) { ESP.free = 29100; ESP.largest = 17396; }
    if (builtPages >= 40) { pageCount = 40; building = partial = false; complete = true; }
    return true;
  }
  bool parkBuild() {
    require(RenderLock::busy, "park without render lock");
    if (!canPark) return false;
    ++parks; parked = true; ESP.free = parkRestoresFree; ESP.largest = parkRestoresLargest;
    return true;
  }
  void suspendBuild() {
    require(RenderLock::busy, "suspend without render lock"); ++suspends;
    oldPages = std::max(oldPages, builtPages); pageCount = oldPages;
    building = parked = false; partial = true; ESP.free = 80000; ESP.largest = 60000;
  }
};
struct Epub {
  int getSpineItemsCount() const { return 3; }
  // Spine of each table of contents entry: spine 2 is one file holding two chapters.
  std::vector<int16_t> tocSpines{0, 1, 2, 2};
  struct TocEntry {
    int16_t spineIndex;
  };
  int getTocItemsCount() const { return static_cast<int>(tocSpines.size()); }
  TocEntry getTocItem(const int i) const { return {tocSpines[i]}; }
  int getTocIndexForSpineIndex(const int spine) const {
    for (size_t i = 0; i < tocSpines.size(); ++i)
      if (tocSpines[i] == spine) return static_cast<int>(i);
    return -1;
  }
  std::string getThumbBmpPath(int height) const { return "/thumb_" + std::to_string(height) + ".bmp"; }
  // Mirrors Epub::generateThumbBmps: an existing file is skipped and costs nothing.
  void generateThumbBmps(const int* heights, int count) const {
    for (int i = 0; i < count; ++i) {
      if (thumbs.has(heights[i])) continue;
      ++thumbs.generated;
      thumbs.heights.push_back(heights[i]);
      thumbs.onCard.push_back(heights[i]);
    }
  }
  // The one-height call of the previous release, so its reader still compiles for a red run.
  bool generateThumbBmp(int height) const { generateThumbBmps(&height, 1); return true; }
};
// The cover page's decode writes the thumbnails through this hook (ImageBlock::ThumbHook).
struct ImageBlock {
  struct ThumbHook { virtual ~ThumbHook() = default; };
  static inline ThumbHook* hook = nullptr;
  static void setThumbHook(ThumbHook* h) { hook = h; }
};
struct CoverThumbCapture : ImageBlock::ThumbHook {
  CoverThumbCapture(const Epub&, const int*, int) {}
};
template <class T, class... A>
std::unique_ptr<T> makeUniqueNoThrow(A&&... args) { return std::make_unique<T>(std::forward<A>(args)...); }
struct Gpio {
  bool usbChanged = false;
  bool wasUsbStateChanged() const { return usbChanged; }
} gpio;
struct Manager {
  bool sleepTransitionState = false;
  uint32_t activityGeneration() const { return 1; }
  bool isSleepTransition() const { return sleepTransitionState; }
  // Writes left for the next screen's first frame (ActivityManager::deferWrite).
  std::vector<void (*)()> deferred;
  void deferWrite(void (*write)()) { deferred.push_back(write); }
  void nextScreenFramed() {
    for (auto write : deferred) write();
    deferred.clear();
  }
} activityManager;
struct EndMenu { bool menuActive() const { return false; } };
// The open's own writes: state.json and the recent list (ReaderActivity::onEnter and commitOpen).
struct OpenWrites { int stateSaves = 0, recentAdds = 0, statsSaves = 0; } openWrites;
struct AppState {
  std::string openEpubPath;
  void saveToFile() { ++openWrites.stateSaves; }
} APP_STATE;
struct RecentBooks {
  void addBook(const std::string&, const std::string&, const std::string&, const std::string&) {
    ++openWrites.recentAdds;
  }
} RECENT_BOOKS;
struct ReadingStats {
  bool activateBook(const std::string&, int, const std::string&) { return true; }
  uint32_t currentDay() const { return 1; }
  void record(uint32_t, uint32_t, uint16_t, int) {}
  void observeHabits(uint32_t, uint16_t, uint32_t) {}
  bool saveToFile() { ++openWrites.statsSaves; return true; }
} READING_STATS;
struct ReaderActivity {
  readerstatus::Pace readingPace;
  virtual readerstatus::Pace::Position pacePosition() const { return {}; }
  int pendingExternalTurn = 0, requests = 0, trangDaLat = 0;
  uint32_t pendingExternalGeneration = 0;
  bool pendingTurnIsLocal = false, pendingExternalChapter = false, preview = false;
  std::atomic<bool> endOfBookOptionsReady{false};
  // What the tail of onEnter(), onTick() and commitOpen() touch.
  std::string bookPath = "/sach/moi.epub";
  bool statsEnabled = false, openCommitPending = false, statsActive = false, statsDirty = false;
  // Set by the reader menu's open (EpubReaderActivity::openReaderMenu); the previous release had
  // no such flag and wrote the stats on every pause.
  bool pauseKeepsStatsInRam = false;
  uint32_t statsLastMs = 0, statsSavedMs = 0, statsDayPollMs = 0, statsDay = 0;
  std::atomic<bool> pageReady{false};
  // A laid-out page (or the end screen) reached the panel; an error screen sets only pageReady.
  std::atomic<bool> pageRendered{false};
  std::string getBookTitle() const { return "Tieu de"; }
  std::string getBookAuthor() const { return "Tac gia"; }
  std::string getBookThumbBmpPath() const { return "/thumb_[HEIGHT].bmp"; }
  struct Info { int progressPercent = 0; };
  Info getScreenshotInfo() const { return {}; }
  bool readingPageVisible() const { return true; }
  void updateReadingTime(bool); void chotSoLieuDoc(); void onPause();
  void openTail(); void onTick(); void commitOpen();
  bleturner::LinkNote linkNoteDrawn = bleturner::LinkNote::None;
  bool linkNoteInTitle = false;
  void redrawLinkNote() { requestUpdate(); }
  std::unique_ptr<EndMenu> endOfBookOptions = std::make_unique<EndMenu>();
  virtual ~ReaderActivity() = default;
  virtual bool latTrangThat(bool) = 0;
  virtual bool nhayChuongThat(int) { return false; }
  bool externalPageTurnAllowed() const { return true; }
  bool manualPageTurnReady() const { return true; }
  virtual bool pageAwaitsLayout() const { return false; }
  bool isAtEndOfBook() const { return false; }
  void onReturnFromEndOfBook() {}
  bool handleEndOfBookPageTurn(bool, bool) { return false; }
  void requestUpdate() { ++requests; }
  bool luotLatTrangNgoai(bool); bool processExternalPageTurn(); bool pageTurnLocked(bool);
  static constexpr int MAX_QUEUED_TURNS = 8;
  void queuePageTurn(bool, bool, const char*);
};
struct EpubReaderActivity : ReaderActivity {
  enum class Overlay { None, Text };
  Overlay overlay = Overlay::None;
  bool toolbarMenu = true;
  bool usesToolbarMenu() const { return toolbarMenu; }
  std::unique_ptr<Section> section = std::make_unique<Section>();
  bool xemTruoc = false, xemTruocTrenMan = false, textSettingsDirty = false;
  int8_t xemTruocLat = 0;
  std::unique_ptr<Section> catchUp;
  std::atomic<uint8_t> textCloseFrame{0};
  std::atomic<bool> pageFrameShown{false};
  class BackgroundBuildFrameGuard {
   public:
    explicit BackgroundBuildFrameGuard(EpubReaderActivity& activity) : activity(activity), loanCount(activity.renderer.frameBufferLoanCount()) {}
    ~BackgroundBuildFrameGuard() {
      if (activity.renderer.frameBufferLoanCount() != loanCount &&
          activity.pageFrameShown.exchange(false, std::memory_order_acq_rel))
        activity.requestUpdate();
    }
    BackgroundBuildFrameGuard(const BackgroundBuildFrameGuard&) = delete;
    BackgroundBuildFrameGuard& operator=(const BackgroundBuildFrameGuard&) = delete;
   private:
    EpubReaderActivity& activity;
    uint32_t loanCount;
  };
  // Preview catch-up is covered by the simulator; reject active preview in this projection.
  void catchUpTick(bool) { require(!xemTruoc, "preview catch-up needs the simulator"); }
  bool catchUpCanTick() const { return xemTruoc; }
  void flushTextSettingsLocked();
  std::unique_ptr<Epub> epub = std::make_unique<Epub>();
  std::unique_ptr<ImageBlock::ThumbHook> coverThumbs;
@@FIELDS@@
  ReaderRenderer renderer;
  // A button edge in this pass; the idle steps wait for a quiet pass.
  int8_t pendingManualTurn = 0;
  // The status bar redrawn alone after a USB edge (EpubReaderActivity::repaintStatusBarAlone).
  bool statusBarStale = false;
  std::atomic<bool> paintDropped{false};
  int statusRepaints = 0;
  void repaintStatusBarAlone() { ++statusRepaints; }
  struct Input { bool edge = false; bool wasAnyPressed() const { return edge; } bool wasAnyReleased() const { return false; } } mappedInput;
  int pagesUntilFullRefresh = 0;
  bool automaticPageTurnActive = false;
  // The reselection a failed build or starved section drops (EpubReaderActivity.h).
  std::string pendingQuoteEdit;
  int currentSpineIndex = 0, nextPageNumber = 0, pendingPageJump = 0;
  uint32_t lastPageTurnTime = 0;
  std::atomic<bool> deferredClearPending{false};
  bool deferBackgroundBuildForBle() const; bool buildTickHeapGate(); bool backgroundBuildStartHeapGate(); bool backgroundBuildCanTick(); bool backgroundBuildWanted() const; void suspendBackgroundBuild();
  bool indexStepDue() const { return false; } void runIndexStep() {}
  // The next chapter's early layout (EpubReaderActivity::prepareNextChapter) is covered by the simulator.
  bool nextChapterDue(bool) { return false; } void prepareNextChapter() {}
  bool releaseHeapForBuild(); bool startBuildFreeingHeap(const ReaderRenderSpec& spec, const std::function<void()>& popupFn = nullptr); bool yieldForRadio(); void showMemoryError(); void settleBuildPopup(); void generatePendingThumb(); void writePendingThumbs();
  void backgroundTick(); void foreground(); bool skipLoopDelay(); bool latTrangThat(bool);
  // loadBook()'s cover-thumbnail tail and loop()'s idle region, projected verbatim.
  void openThumbStep(); void idleStep();
  void initialResume(int target);
  void percentJump();
  // A chapter jump's anchor, resolved once its section is laid out (renderBook).
  std::string pendingAnchor;
  void anchorLanding();
  // Progress write owed by a paint that skipped it (EpubReaderActivity::saveProgressIfMoved).
  std::atomic<bool> progressSaveDeferred{false};
  bool progressSaveFailed = false;
  int lastSavedSpineIndex = -1, lastSavedPage = -1, lastSavedPageCount = -1;
  int saveAttempts = 0; bool saveWorks = true;
  bool saveProgress(int, int, int) { ++saveAttempts; return saveWorks; }
  void saveProgressIfMoved();
  // A jump the heap could not lay out: the reader forgets it (EpubReaderActivity::forgetPendingJump).
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  int forgottenJumps = 0;
  void forgetPendingJump() { ++forgottenJumps; pendingPercentJump = false; pendingAnchor.clear(); }
  // Where a starved jump or open leaves the reader (EpubReaderActivity::stayAfterStarvedJump).
  void stayAfterStarvedJump();
  void showBuildPopup(GfxRenderer&, int&);
  void loadPageForRender();
  // What renderBook goes on to after the layout: the reposition, then loading and drawing the page.
  int repositions = 0;
  bool applyDeferredReposition() { ++repositions; return false; }
  // Why the page being painted is about to be replaced (EpubReaderActivity::nextScreenWaiting).
  const char* nextScreen = nullptr;
  const char* nextScreenWaiting() const { return nextScreen; }
@@LAYOUT@@
};
