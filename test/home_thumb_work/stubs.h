#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
int powerLocks = 0, decodes = 0, holders = 0, popups = 0;
bool fullSpeed = true, fileWritten = false, nextWrite = true;
uint32_t millis() { return 10000; }
constexpr int HOLDER_IDLE_MS = 1000;
constexpr size_t CARD_THUMB_MIN_FREE_HEAP = 100000, CARD_THUMB_REF_MIN_FREE_HEAP = 80000;
constexpr size_t CARD_THUMB_MIN_LARGEST_BLOCK = 32768;
struct HalPowerManager { struct Lock { Lock() { ++powerLocks; } ~Lock() { --powerLocks; } }; };
struct RenderLock { template<class T> explicit RenderLock(T&) {} };
namespace tenorchrome { bool kTouchShell = true; }
struct { bool pressed = false, released = false; bool wasAnyPressed() { return pressed; } bool wasAnyReleased() { return released; } } mappedInput;
struct Gpio { bool held = false; bool rawInputActive() { return held; } };
[[maybe_unused]] Gpio gpio;
struct { size_t heap = 200000; size_t getFreeHeap() { return heap; } size_t getMaxAllocHeap() { return heap; } } ESP;
struct { bool exists(const char*) { return fileWritten; } } Storage;
namespace coverref { bool load(const std::string&, std::string& href) { href = "cover.jpg"; return true; } }
struct Epub {
  Epub(const std::string&, const char*) {}
  std::string getCachePath() { return "/cache"; }
  std::string getCoverHref() { return "cover.jpg"; }
  bool load(bool, bool) { return true; }
  template<class... T> void generateThumbBmps(T...) { ++decodes; fileWritten = nextWrite; fullSpeed &= powerLocks > 0; }
};
struct FsHelpers { static bool hasEpubExtension(const std::string&) { return true; } };
struct { void drawPopup(int, int) { ++popups; } } GUI;
constexpr int STR_LOADING_POPUP = 0;
int tr(int v) { return v; }
struct UITheme {
  int homeCoverHeight = 226;
  static UITheme& getInstance() { static UITheme t; return t; }
  UITheme& getMetrics() { return *this; }
};
template<class... T> void logLine(T...) {}
#define LOG_INF(...) logLine(__VA_ARGS__)
#define LOG_PROBE(...) logLine(__VA_ARGS__)
uint32_t thumbTried[5] = {};
uint8_t thumbTriedNext = 0;
uint32_t holderId(int w, int h, bool g) { return w * 1000 + h + g; }
struct HomeActivity {
  enum class Tab { RECENT, FILES };
  Tab activeTabId = Tab::RECENT;
  struct Book { std::string path, coverBmpPath; };
  std::vector<Book> recentBooks{{"/a.epub", "/a"}, {"/b.epub", "/b"}, {"/c.epub", "/c"}, {"/d.epub", "/d"}};
  struct Request { int book = 0, width = 100, height = 150; };
  std::array<Request, 4> touchThumbs{};
  int touchThumbCount = 0;
  bool thumbBatchWrote = false;
  bool ink = true, homeGraySelected = false, coverRendered = true;
  int cardThumbWidth = 298, cardThumbHeight = 450, renderer = 0, updates = 0;
  std::atomic<int8_t> thumbWanted{-1};
  std::atomic<uint32_t> thumbWantedAtMs{0};
  struct Holder { int width = 100, height = 150; };
  std::array<Holder, 4> holderRequests{};
  std::atomic<int> holderCount{0};
  std::atomic<uint32_t> holderAtMs{0};
  uint32_t holderTried[8]{};
  unsigned holderTriedNext = 0;
  int shownRecent() { return 0; }
  bool homeInkEnabled() { return ink; }
  void freeCoverBuffer() {}
  void requestUpdate() { ++updates; }
  std::string touchCoverPath(const std::string& p, int, int) { return p; }
  bool writeHolder(int, int) { ++holders; fullSpeed &= powerLocks > 0; return true; }
  void writeMissingThumb(int);
  bool tickHolders();
  void queueTouchThumb(int, int, int);
};
