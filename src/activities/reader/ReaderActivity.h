#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "EndOfBookOptions.h"
#include "activities/Activity.h"

// Page-turn and paint trace lines (RDR_TRACE, ERS_TRACE). Acceptance builds carry them,
// and so does the press probe, which measures latency on an otherwise release build.
#if defined(TENOR_UI_ACCEPTANCE) || defined(TENOR_PRESS_PROBE)
#define TENOR_TURN_TRACE 1
#endif

class ReaderActivity : public Activity {
 protected:
  std::string bookPath;
  int pagesUntilFullRefresh = 0;
  bool forcedRefreshPending = false;
  bool preview = false;
  static constexpr uint8_t PREVIEW_FOOTER_HEIGHT = 36;
  bool handlePreviewInput();
  void drawPreviewFooter() const;
  uint8_t readerStatusBarHeight() const;
  void readingMargins(int& top, int& right, int& bottom, int& left) const;

  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};

  explicit ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string bookPath, bool allowFastInitialRefresh);

  virtual bool loadBook() = 0;
  virtual std::string getBookTitle() const = 0;
  virtual std::string getBookAuthor() const { return ""; }
  virtual std::string getBookThumbBmpPath() const { return ""; }

  virtual bool handleFormatInput() { return false; }
  // Lat trang THAT, moi dinh dang sach tu viet. KHONG goi thang ham nay; goi pageTurn()
  // o duoi, vi do la cho duy nhat dem so trang da lat.
  virtual bool latTrangThat(bool isForward) = 0;

  // Lat mot trang, va DEM no. Mot cho duy nhat dem, chu khong dem o ca ba trinh doc:
  // dem o ba noi thi mot dinh dang moi se im lang khong duoc dem.
  // The format mutation runs under this one nonrecursive render lock.
  bool pageTurn(bool isForward);
  bool pageTurnLocked(bool isForward);
  virtual bool externalPageTurnAllowed() const { return !preview; }
  virtual bool manualPageTurnReady() const { return true; }
  // True while the page on screen is one the layout has not reached yet (a chapter still
  // being laid out). Queued turns wait there for the paint that lays it out: run on
  // past it, they would leave the chapter's real end and the paint would pull them back.
  virtual bool pageAwaitsLayout() const { return false; }
  // Applies the queued turns once the render lock is free. Returns true only when it
  // spent the pass (end of book, a chapter jump). While it waits, and after plain page
  // turns, it returns false, so the caller still reads this pass's press: the edge latch
  // hands each press to one pass only, and a return here would lose it.
  bool processExternalPageTurn();
  // Signed count of queued page turns. Every local press that lands during a paint
  // counts and opposite presses cancel, capped so a stuck source stays bounded. A
  // remote report or a chapter jump still replaces the queue with its own direction.
  static constexpr int8_t MAX_QUEUED_TURNS = 8;
  void queuePageTurn(bool isForward, bool isLocal, const char* reason);
  // Atomic: the render task reads it to drop a paint nobody will see (nextScreenWaiting).
  std::atomic<int8_t> pendingExternalTurn{0};
  bool pendingTurnIsLocal = false;
  bool pendingExternalChapter = false;
  uint32_t pendingExternalGeneration = 0;
#ifdef TENOR_TURN_TRACE
  struct TurnTrace {
    uint32_t id = 0;
    unsigned long detectedMs = 0;
    const char* source = "none";
    bool forward = false;
  };
  uint32_t turnTraceSequence = 0;
  TurnTrace currentTurnTrace;
  TurnTrace pendingExternalTurnTrace;
  // Written and read only under RenderLock. Input queues have separate records.
  TurnTrace appliedTurnTrace;
  TurnTrace detectTurnTrace(const char* source, bool forward);
  void logTurnTrace(const char* phase, const TurnTrace& trace, const char* detail) const;
  // merged: the queued press still turns its page with this batch; otherwise it is displaced.
  void replaceQueuedTurnTrace(TurnTrace& queue, const TurnTrace& incoming, const char* reason, bool merged = false);
  void dropTurnTrace(TurnTrace& trace, const char* reason);
#endif
  virtual bool skipPages(int amount) { return pageTurn(amount > 0); }
  // Nhay DUNG MOT chuong theo `huong` (+1 toi, -1 lui). Khoa ve da duoc nguoi goi
  // giu san. Trinh doc khong co muc luc tra ve false: giu nut o do khong lam gi.
  virtual bool nhayChuongThat(int /*huong*/) { return false; }
  // Giu nut lat trang khi "Giu nut lat trang khi doc" = Co chu: doi co mot nac theo `huong`
  // (+1 to, -1 nho), KEP o hai bien. Tra ve true neu co doi. Mac dinh (XTC, bitmap) khong doi gi,
  // nhung nhip giu van bi TIEU: 0 doi co, 0 lat trang du.
  virtual bool docCoChuMotNac(int /*huong*/) { return false; }
  virtual bool isAtEndOfBook() const = 0;
  virtual void onReturnFromEndOfBook() {}

  virtual void renderBook() = 0;
  virtual void applyInitialOrientation();
  virtual void onEndOfBookRendered() {}

  bool handleBackNavigation();
  // Set once Back has asked to leave. The render task reads it: a paint still running then skips
  // its gray pass instead of holding the exit behind it.
  std::atomic<bool> leaving{false};
  void stayAfterDroppedExit();
  /** True while the end-of-book suggestion menu is on screen and owning input. */
  bool endOfBookMenuActive() const;
  bool handleEndOfBookMenu(bool suppressConfirmRelease = false);
  bool handleEndOfBookPageTurn(bool prevTriggered, bool nextTriggered);
  void clearEndOfBookOptionsIfNeeded();
  void disableFastInitialRefresh();

 public:
  ~ReaderActivity() override = default;
  std::string navigationMemoryKey() const override { return name + ":" + bookPath; }

  // Queue one external direction for this reader generation. The reader loop
  // applies render/menu guards, counts the accepted turn and requests repaint.
  bool luotLatTrangNgoai(bool isForward);

  // Nhu tren, nhung mot nac CHUONG. Cung mot hang doi nen hai lenh khong chong nhau:
  // lenh sau thay lenh truoc, giong hai luot lat trang lien tiep.
  bool luotNhayChuongNgoai(bool isForward);

  // A shortcut asked from outside the reader's own keys (ActivityManager::readerShortcut).
  // A format that has it runs it on its next pass; the others refuse it.
  virtual bool requestShortcut(ReaderShortcut) { return false; }

  // True while the reader keeps the page-turner radio from starting: a book still building its
  // index in the background needs the heap the radio would take.
  virtual bool holdsRadio() const { return false; }

  static std::unique_ptr<ReaderActivity> create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                std::string path, bool allowFastInitialRefresh, bool preview = false);

  void onEnter() override;
  void onExit() override;
  void onTick() override;
  void onPause() override;
  void onResume() override;

 protected:
  virtual bool readingPageVisible() const { return !isAtEndOfBook(); }
  bool statsEnabled = false;
  bool statsActive = false;
  bool statsDirty = false;
  std::atomic<bool> pageReady{false};
  uint32_t statsLastMs = 0;
  uint32_t statsSavedMs = 0;
  uint32_t statsDay = 0;
  uint32_t statsDayPollMs = 0;
  uint16_t trangDaLat = 0;
  void updateReadingTime(bool active);
  void chotSoLieuDoc();
  // state.json and the recent list are read by the next boot and by Home, never before the
  // first frame of this reader, yet writing them sat on the open path in front of that frame.
  // onEnter() leaves them pending; the first tick after the frame, or the exit, writes them.
  bool openCommitPending = false;
  void commitOpen();
  // Set for the reader menu only: writing the stats checkpoint held the menu back 245 ms on the
  // X3. The record stays in RAM and is written by the next 30 s checkpoint once the menu has
  // closed, or by onExit() (sleep included), so a crash in the menu loses at most the reading
  // since the last checkpoint, the same bound as a crash on the page.
  bool pauseKeepsStatsInRam = false;
  // The page's excerpt for the recent card. Home reads it from RAM; recent.json is rewritten by
  // onExit() after Home's first frame, so a power cut before then loses only the excerpt.
  void rememberExcerpt(const std::string& text);

 public:
  void loop() override;
  bool isPageReady() const { return pageReady.load(std::memory_order_acquire); }
  void render(RenderLock&& lock) override;

  bool isReaderActivity() const final { return !preview; }
  bool handleForcedRefresh() final;
};
