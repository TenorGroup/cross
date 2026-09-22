#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <algorithm>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define STR_INDEXING 1
#define STR_PAGE_LOAD_ERROR 2
#define STR_MEMORY_ERROR 3
#define UI_12_FONT_ID 12
struct EpdFontFamily { static constexpr int BOLD = 1; };
int tr(int n) { return n; }
uint32_t clockMs = 1000;
uint32_t millis() { return clockMs; }
int popupCount = 0, buildErrors = 0;
uint32_t popupAtMs = 0;
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
struct Settings { bool blePageTurnerEnabled = false; ReaderRenderSpec readerRenderSpec(int, int) { return {}; } } SETTINGS;
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
inline bool stopForIdle() { ++stopForIdleCalls; idleStoppedState = true; return true; }
inline void requestRearm() { ++rearmRequests; }
}  // namespace freeink::ble
inline void delay(uint32_t ms) { clockMs += ms; }
struct Gui { void drawPopup(int, int) { ++popupCount; popupAtMs = millis(); } } GUI;
struct PageReadState { int failures = 0, reads = 0, clears = 0, abandons = 0, errors = 0; } pageReads;
struct ReaderRenderer {
  operator int() const { return 0; }
  bool hasFrameBuffer() const { return true; }
  void clearScreen() {}
  void drawCenteredText(int, int, int message, bool, int) { if (message == STR_PAGE_LOAD_ERROR) ++pageReads.errors; }
  void displayBuffer() {}
};
using GfxRenderer = ReaderRenderer;
struct Section {
  int currentPage = 4, pageCount = 19, oldPages = 19, builtPages = 4;
  bool building = true, partial = true, complete = false, parked = false, canPark = false;
  bool failStart = false, failTick = false, dropAfterTick = false, dropAfterStart = false;
  bool starveUntilRadioStopped = false, starved = false;
  bool buildStarved() const { return starved; }
  int starts = 0, ticks = 0, suspends = 0, parks = 0, resumes = 0;
  int restoredPagesAfterStart = 0;
  uint32_t startMs = 0, tickMs = 0;
  struct Page {};
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
  std::optional<int> findAnchor(const std::string&) const { return std::nullopt; }
  bool buildReachedVisibleTextOffset(uint32_t) const { return false; }
  bool startBuild(const ReaderRenderSpec&) { require(RenderLock::busy, "start without render lock"); ++starts; clockMs += startMs; if (failStart) return false; building = true; parked = false; builtPages = restoredPagesAfterStart; if (dropAfterStart) ESP.free = 29000; return true; }
  bool buildSomeMore(int n) {
    require(RenderLock::busy, "build without render lock"); ++ticks; clockMs += tickMs;
    if (parked) { parked = false; ++resumes; }
    if (failTick) return false;
    starved = starveUntilRadioStopped && !freeink::ble::idleStoppedState;
    if (starved) { parked = canPark; return false; }
    builtPages += n; pageCount = std::max(oldPages, builtPages);
    if (dropAfterTick) { ESP.free = 29100; ESP.largest = 17396; }
    if (builtPages >= 40) { pageCount = 40; building = partial = false; complete = true; }
    return true;
  }
  bool parkBuild() {
    require(RenderLock::busy, "park without render lock");
    if (!canPark) return false;
    ++parks; parked = true; ESP.free = 80000; ESP.largest = 60000;
    return true;
  }
  void suspendBuild() {
    require(RenderLock::busy, "suspend without render lock"); ++suspends;
    oldPages = std::max(oldPages, builtPages); pageCount = oldPages;
    building = parked = false; partial = true; ESP.free = 80000; ESP.largest = 60000;
  }
};
struct Epub { int getSpineItemsCount() const { return 3; } };
struct Manager { uint32_t activityGeneration() const { return 1; } } activityManager;
struct EndMenu { bool menuActive() const { return false; } };
struct ReaderActivity {
  int pendingExternalTurn = 0, requests = 0, trangDaLat = 0;
  uint32_t pendingExternalGeneration = 0;
  bool pendingTurnIsLocal = false, preview = false;
  std::atomic<bool> endOfBookOptionsReady{false};
  std::unique_ptr<EndMenu> endOfBookOptions = std::make_unique<EndMenu>();
  virtual ~ReaderActivity() = default;
  virtual bool latTrangThat(bool) = 0;
  bool externalPageTurnAllowed() const { return true; }
  bool manualPageTurnReady() const { return true; }
  bool isAtEndOfBook() const { return false; }
  void onReturnFromEndOfBook() {}
  bool handleEndOfBookPageTurn(bool, bool) { return false; }
  void requestUpdate() { ++requests; }
  bool luotLatTrangNgoai(bool); bool processExternalPageTurn(); bool pageTurnLocked(bool);
};
struct EpubReaderActivity : ReaderActivity {
  std::unique_ptr<Section> section = std::make_unique<Section>();
  std::unique_ptr<Epub> epub = std::make_unique<Epub>();
@@FIELDS@@
  ReaderRenderer renderer;
  int pagesUntilFullRefresh = 0;
  bool automaticPageTurnActive = false;
  int currentSpineIndex = 0, nextPageNumber = 0, pendingPageJump = 0;
  uint32_t lastPageTurnTime = 0;
  std::atomic<bool> deferredClearPending{false};
  bool deferBackgroundBuildForBle() const; bool buildTickHeapGate(); bool backgroundBuildStartHeapGate(); bool backgroundBuildCanTick(); void suspendBackgroundBuild();
  bool releaseRadioForBuild(); void showMemoryError();
  void backgroundTick(); void foreground(); bool skipLoopDelay(); bool latTrangThat(bool);
  void initialResume(int target);
  void showBuildPopup(GfxRenderer&, int&);
  void loadPageForRender();
  bool applyDeferredReposition() { return false; }
};
