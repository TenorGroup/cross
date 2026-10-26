#include <algorithm>
#include <atomic>
#include <vector>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <functional>

#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
unsigned long nowMs = 1000;
unsigned long millis() { return nowMs; }

struct Epub {
  int spines = 3;
  int getSpineItemsCount() const { return spines; }
};
// A chapter laid out: its pages, and how much text a page holds (a text size change moves the page boundaries).
struct Section {
  int currentPage = 0;
  uint16_t pageCount = 1;
  uint32_t charsPerPage = 100;
  std::optional<int> getPageForVisibleTextOffset(uint32_t offset) const {
    return static_cast<int>(std::min<uint32_t>(offset / charsPerPage, pageCount - 1u));
  }
  bool isBuilding() const { return false; }
  bool isPartial() const { return false; }
  void abandonBuild() {}
};
struct EpubReaderActivity {
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section;
  int currentSpineIndex = 0, nextPageNumber = 0, cachedSpineIndex = -1, cachedChapterTotalPageCount = 0;
  std::optional<uint16_t> pendingPageJump;
  std::optional<uint32_t> pendingOffsetJump, cachedVisibleTextOffset;
  bool pendingPercentJump = false, xemTruoc = false, xemTruocTrenMan = false;
  std::string pendingAnchor;
  std::atomic<bool> deferredClearPending{false};
  unsigned long lastPageTurnTime = 0, xemTruocInputMs = 0;
  int xemTruocLat = 0, catchUpFails = 0;
  uint32_t xemTruocDich = 0;
  struct Pace {} readingPace;
  // The text layout in force: what each chapter lays out to (pages), and the page size (characters).
  std::vector<int> pages{10, 4, 7};
  uint32_t charsPerPage = 100;
  int savedSpine = -1, savedPage = -1;  // the progress the last paint stored

  std::unique_ptr<Section> layoutChapter(int spine) const {
    auto made = std::make_unique<Section>();
    made->pageCount = static_cast<uint16_t>(pages[spine]);
    made->charsPerPage = charsPerPage;
    return made;
  }
  void rememberCurrentContentOffset() { cachedVisibleTextOffset = section->currentPage * section->charsPerPage; }
  void dropCatchUp() {}
  void clearDeferredReposition() {}

  bool latTrangThat(bool isForwardTurn);
  void onReturnFromEndOfBook();
  void danLaiTrang();
  void paint();  // renderBook's chapter load and landing
};
