#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#define TRACE_PRESENT @@TRACE_PRESENT@@
struct LogEvent { std::string tag, text; };
std::vector<LogEvent> events;
void captureLog(const char* tag, const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  events.push_back({tag, buffer});
}
#define LOG_INF(tag, ...) captureLog(tag, __VA_ARGS__)
#define LOG_DBG(tag, ...) captureLog(tag, __VA_ARGS__)

unsigned long nowMs = 1000;
unsigned long millis() { return nowMs; }
struct Heap {
  unsigned getFreeHeap() const { return 80000; }
  unsigned getMaxAllocHeap() const { return 60000; }
  unsigned getMinFreeHeap() const { return 48000; }
} ESP;
struct RenderLock {
  struct TryTake {};
  static inline bool busy = false;
  bool owns = false;
  explicit RenderLock(TryTake) { if (!busy) busy = owns = true; }
  ~RenderLock() { if (owns) busy = false; }
  bool acquired() const { return owns; }
  static bool peek() { return busy; }
};
struct ActivityManager {
  uint32_t generation = 1;
  uint32_t activityGeneration() const { return generation; }
  // Writes left for the next screen's first frame (deferWrite); nextScreenFramed runs them.
  std::vector<void (*)()> deferred;
  void deferWrite(void (*write)()) { deferred.push_back(write); }
  void nextScreenFramed() {
    for (auto write : deferred) write();
    deferred.clear();
  }
} activityManager;
struct EndOfBookOptions {
  bool menu = false;
  bool menuActive() const { return menu; }
};
int cardStatsSaves = 0;
struct ReadingStats {
  uint32_t currentDay() const { return 1; }
  bool saveToFile() { ++cardStatsSaves; return true; }
} READING_STATS;
// What one pass of ReaderActivity::loop reads from the buttons; a test sets it before the pass.
struct Renderer {};
struct MappedInput {
  unsigned long getHeldTime() const { return 0; }
};
struct PageTurns { bool prev = false, next = false, prevLongPressed = false, nextLongPressed = false, fromTilt = false; };
struct TouchTurns { bool prev = false, next = false; unsigned long heldMs = 0; };
PageTurns passTurns;
namespace ReaderUtils {
constexpr unsigned long SKIP_HOLD_MS = 700;
TouchTurns detectTouchPageTurn(Renderer&, MappedInput&) { return {}; }
PageTurns detectPageTurn(MappedInput&) {
  const PageTurns turns = passTurns;
  passTurns = {};
  return turns;
}
}  // namespace ReaderUtils
struct Settings {
  enum { FONT_SIZE_STEP = 1, CHAPTER_SKIP = 2 };
  uint8_t longPressButtonBehavior = 0;
} SETTINGS;
// The page turner module: a page turn gives a Failed link note back to the title.
namespace bleturner {
inline void acknowledgeLinkNote() {}
}  // namespace bleturner
struct ReaderActivity {
  static constexpr int8_t MAX_QUEUED_TURNS = 8;
  int8_t pendingExternalTurn = 0;
  bool pendingTurnIsLocal = false;
  bool pendingExternalChapter = false;
  uint32_t pendingExternalGeneration = 0;
@@TRACE_FIELDS@@
  std::atomic<bool> endOfBookOptionsReady{false};
  std::unique_ptr<EndOfBookOptions> endOfBookOptions = std::make_unique<EndOfBookOptions>();
  bool preview = false;
  uint16_t trangDaLat = 0;
  bool pauseKeepsStatsInRam = false;
  int updates = 0;
  bool ready = true;
  bool allowed = true;
  bool changed = true;
  bool atEnd = false;
  int modelPage = 1;
  uint32_t statsLastMs = 0, statsDayPollMs = 0, statsDay = 0, statsSavedMs = 0;
  bool statsActive = false, statsEnabled = true, statsDirty = false;
  Renderer renderer;
  MappedInput mappedInput;
  virtual ~ReaderActivity() = default;
  bool externalPageTurnAllowed() const { return allowed; }
  bool manualPageTurnReady() const { return ready; }
  virtual bool pageAwaitsLayout() const { return false; }
  bool nhayChuongThat(int) { return false; }
  bool docCoChuMotNac(int) { return false; }
  virtual bool skipPages(int amount) { return pageTurn(amount > 0); }
  void clearEndOfBookOptionsIfNeeded() {}
  bool handlePreviewInput() { return false; }
  bool handleEndOfBookMenu() { return false; }
  bool handleFormatInput() { return false; }
  bool handleBackNavigation() { return false; }
  // Raised by Back; the reader's loop runs again only when that exit was dropped.
  std::atomic<bool> leaving{false};
  void stayAfterDroppedExit();
  void loop();
  virtual bool latTrangThat(bool forward) {
    if (!changed) return false;
    modelPage += forward ? 1 : -1;
    return true;
  }
  bool isAtEndOfBook() const { return atEnd; }
  void onReturnFromEndOfBook() { atEnd = false; }
  bool handleEndOfBookPageTurn(bool, bool) { return false; }
  void requestUpdate() { ++updates; }
  void updateReadingTime(bool) {}
  int statsSaves = 0;
  void chotSoLieuDoc() { ++statsSaves; }
  // The open's state and recent-list writes; the turn queue never depends on them.
  void commitOpen() {}
  virtual void onPause();
  virtual void onResume();
  virtual void onExit() {}
  bool pageTurn(bool);
  bool pageTurnLocked(bool);
  bool luotLatTrangNgoai(bool);
  void queuePageTurn(bool, bool, const char*);
  bool processExternalPageTurn();
};
// A chapter as far as it is laid out. Until layout() sets it, a long laid-out chapter that
// keeps the plain page model above.
struct Section {
  bool modelled = false;
  int currentPage = 0;
  uint16_t pageCount = 1000;
  bool building = false;
  bool isBuilding() const { return building; }
  bool isPartial() const { return false; }
};
struct EpubReaderActivity : ReaderActivity {
  std::unique_ptr<Section> section = std::make_unique<Section>();
  int chapter = 0;
  int chapters = 2;
  // Cleared by onPause: the screen over the reader draws into the framebuffer.
  std::atomic<bool> pageFrameShown{false};
@@EPUB_FIELDS@@
  void layout(const int page, const int pages, const bool building) {
    if (!section) section = std::make_unique<Section>();
    section->currentPage = page;
    section->pageCount = static_cast<uint16_t>(pages);
    section->building = building;
    section->modelled = true;
  }
  // The production turn (EpubReaderActivity::latTrangThat) on a chapter model: while the chapter
  // is still being laid out a forward turn may step past the pages laid out so far.
  bool latTrangThat(bool forward) override {
    if (!section) return false;
    if (!section->modelled) return ReaderActivity::latTrangThat(forward);
    if (forward) {
      if (section->currentPage < section->pageCount - 1 || section->isBuilding() || section->isPartial()) {
        section->currentPage++;
      } else if (chapter + 1 < chapters) {
        ++chapter;
        section.reset();
      } else {
        atEnd = true;
      }
      return true;
    }
    if (section->currentPage > 0) {
      section->currentPage--;
      return true;
    }
    if (chapter == 0) return false;
    --chapter;
    section.reset();
    return true;
  }
@@EPUB_LAYOUT@@
  // The paint's last step once the chapter is laid out (EpubReaderActivity::renderBook).
  void settleLaidOut(bool turnPastLaidOut);
  void drainManual();
  void manualInput(bool prevTriggered, bool prevPageTriggered, bool touchTriggered, bool fromTilt);
  void drainThenInput(bool prevTriggered, bool prevPageTriggered, bool touchTriggered, bool fromTilt);
  void externalThenInput(bool prevTriggered, bool prevPageTriggered, bool touchTriggered, bool fromTilt);
  void cancelManualForReaderMenu();
  bool xemTruoc = false;
  void panelClosedLocked(bool,bool) {}
  void panelClosed(bool leaving = false, bool frameUp = false) {}
  void dropCatchUp() {}
@@EPUB_ON_PAUSE@@
  void onExit() override;
};
