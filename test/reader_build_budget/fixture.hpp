#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <algorithm>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define STR_INDEXING 1
int tr(int n) { return n; }
uint32_t millis() { return 1000; }
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
struct Gui { void drawPopup(int, int) {} } GUI;
struct Section {
  int currentPage = 4, pageCount = 19, oldPages = 19, builtPages = 4;
  bool building = true, partial = true, complete = false, failStart = false, dropAfterTick = false, dropAfterStart = false;
  int starts = 0, ticks = 0, suspends = 0;
  bool isBuilding() const { return building; }
  bool isPartial() const { return partial; }
  bool isBuildComplete() const { return complete; }
  bool startBuild(const ReaderRenderSpec&) { require(RenderLock::busy, "start without render lock"); ++starts; if (failStart) return false; building = true; builtPages = 0; if (dropAfterStart) ESP.free = 29000; return true; }
  bool buildSomeMore(int n) {
    require(RenderLock::busy, "build without render lock"); ++ticks;
    builtPages += n; pageCount = std::max(oldPages, builtPages);
    if (dropAfterTick) { ESP.free = 29100; ESP.largest = 17396; }
    if (builtPages >= 40) { pageCount = 40; building = partial = false; complete = true; }
    return true;
  }
  void suspendBuild() {
    require(RenderLock::busy, "suspend without render lock"); ++suspends;
    oldPages = std::max(oldPages, builtPages); pageCount = oldPages;
    building = false; partial = true; ESP.free = 80000; ESP.largest = 60000;
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
  int renderer = 0, pagesUntilFullRefresh = 0;
  int currentSpineIndex = 0, nextPageNumber = 0, pendingPageJump = 0;
  uint32_t lastPageTurnTime = 0;
  std::atomic<bool> deferredClearPending{false};
  bool deferBackgroundBuildForBle() const; bool buildTickHeapGate(); bool backgroundBuildStartHeapGate(); void suspendBackgroundBuild();
  void backgroundTick(); void foreground(); bool skipLoopDelay(); bool latTrangThat(bool);
  bool applyDeferredReposition() { return false; }
};
