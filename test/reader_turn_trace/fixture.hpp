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
} activityManager;
struct EndOfBookOptions {
  bool menu = false;
  bool menuActive() const { return menu; }
};
struct ReadingStats { uint32_t currentDay() const { return 1; } } READING_STATS;
struct ReaderActivity {
  int8_t pendingExternalTurn = 0;
  bool pendingTurnIsLocal = false;
  uint32_t pendingExternalGeneration = 0;
@@TRACE_FIELDS@@
  std::atomic<bool> endOfBookOptionsReady{false};
  std::unique_ptr<EndOfBookOptions> endOfBookOptions = std::make_unique<EndOfBookOptions>();
  bool preview = false;
  uint16_t trangDaLat = 0;
  int updates = 0;
  bool ready = true;
  bool allowed = true;
  bool changed = true;
  bool atEnd = false;
  int modelPage = 1;
  uint32_t statsLastMs = 0, statsDayPollMs = 0, statsDay = 0;
  bool statsActive = false;
  bool externalPageTurnAllowed() const { return allowed; }
  bool manualPageTurnReady() const { return ready; }
  bool latTrangThat(bool forward) {
    if (!changed) return false;
    modelPage += forward ? 1 : -1;
    return true;
  }
  bool isAtEndOfBook() const { return atEnd; }
  void onReturnFromEndOfBook() { atEnd = false; }
  bool handleEndOfBookPageTurn(bool, bool) { return false; }
  void requestUpdate() { ++updates; }
  void updateReadingTime(bool) {}
  void chotSoLieuDoc() {}
  virtual void onPause();
  virtual void onResume();
  virtual void onExit() {}
  bool pageTurn(bool);
  bool pageTurnLocked(bool);
  bool luotLatTrangNgoai(bool);
  bool processExternalPageTurn();
};
struct Section {};
struct EpubReaderActivity : ReaderActivity {
  std::unique_ptr<Section> section = std::make_unique<Section>();
@@EPUB_FIELDS@@
  void drainManual();
  void manualInput(bool prevTriggered, bool prevPageTriggered, bool touchTriggered, bool fromTilt);
  void cancelManualForReaderMenu();
@@EPUB_ON_PAUSE@@
  void onExit() override;
};
