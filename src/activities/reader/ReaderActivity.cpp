#include "ReaderActivity.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderActivity.h"
#include "ReaderUtils.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "TxtReaderActivity.h"
#include "XtcReaderActivity.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/NgayGio.h"

ReaderActivity::ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string bookPath, const bool allowFastInitialRefresh)
    : Activity(name, renderer, mappedInput), bookPath(std::move(bookPath)) {
  if (allowFastInitialRefresh) {
    const int refreshFrequency = SETTINGS.getRefreshFrequency();
    pagesUntilFullRefresh = refreshFrequency > 1 ? refreshFrequency : 2;
  }
}

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh,
                                                       const bool preview) {
  // ActivityManager requires heap ownership; each branch allocates exactly one screen-lifetime object.
  std::unique_ptr<ReaderActivity> activity;
  if (FsHelpers::hasXtcExtension(path)) {
    activity = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    activity = makeUniqueNoThrow<TxtReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else {
    activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  }

  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  if (activity) activity->preview = preview;
  return activity;
}

void ReaderActivity::applyInitialOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

void ReaderActivity::disableFastInitialRefresh() { pagesUntilFullRefresh = 0; }

void ReaderActivity::onEnter() {
  Activity::onEnter();

  // Heap ledger for field crash reports: free vs largest block distinguishes a
  // leak (free falls) from fragmentation (free stable, largest collapses).
  LOG_INF("MEM", "reader enter: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (!Storage.exists(bookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
    finish();
    return;
  }

  trangDaLat = 0;

#ifdef TENOR_TURN_TRACE
  const unsigned long openStarted = millis();
#endif
  sdFontSystem.ensureLoaded(renderer);
  applyInitialOrientation();
#ifdef TENOR_TURN_TRACE
  const unsigned long fontsDone = millis();
#endif

  if (!loadBook()) {
    finish();
    return;
  }
#ifdef TENOR_TURN_TRACE
  const unsigned long bookLoaded = millis();
#endif

  if (preview) {
    LOG_INF("READER", "Preview: %s", bookPath.c_str());
    requestUpdate();
    return;
  }

  statsEnabled = READING_STATS.activateBook(bookPath, getScreenshotInfo().progressPercent, getBookTitle());
  statsLastMs = statsSavedMs = statsDayPollMs = millis();
  statsDay = READING_STATS.currentDay();
#ifdef TENOR_TURN_TRACE
  LOG_INF("READER", "OPEN_STAGES fonts=%lu load=%lu stats=%lu", fontsDone - openStarted, bookLoaded - fontsDone,
          statsLastMs - bookLoaded);
#endif

  APP_STATE.openEpubPath = bookPath;
  openCommitPending = true;
  requestUpdate();
}

void ReaderActivity::commitOpen() {
  if (!openCommitPending) return;
  // Only a book that put a page on the panel is remembered: one that cannot be laid out (an
  // index or build error screen) must not come back on wake or head the Recent list.
  if (!pageRendered.load(std::memory_order_acquire)) return;
  openCommitPending = false;
#ifdef TENOR_TURN_TRACE
  const unsigned long started = millis();
#endif
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath());
#ifdef TENOR_TURN_TRACE
  LOG_INF("READER", "OPEN_COMMIT ms=%lu", millis() - started);
#endif
}

void ReaderActivity::onExit() {
  Activity::onExit();
  commitOpen();
  if (openCommitPending) {
    // No page ever reached the panel: forget the book, so the state saved below does not reopen it.
    openCommitPending = false;
    if (APP_STATE.openEpubPath == bookPath) APP_STATE.openEpubPath.clear();
  }
  pendingExternalTurn = 0;
#ifdef TENOR_TURN_TRACE
  dropTurnTrace(pendingExternalTurnTrace, "exit");
#endif

  updateReadingTime(false);
  // The stats checkpoint and state.json held the screen after the book 250 to 450 ms on the X3.
  // Both copy what is in RAM, so they wait for that screen's first frame (ActivityManager::
  // deferWrite); a power cut in between loses the reading since the last 30 s checkpoint, the
  // bound a crash on the page already has. Sleep writes them now: the device powers down next.
  const bool sleeping = activityManager.isSleepTransition();
  if (sleeping) {
    chotSoLieuDoc();
  } else if (statsEnabled && statsDirty) {
    activityManager.deferWrite([] { READING_STATS.saveToFile(); });
  }
  // The SD font glyph arenas built while reading are dead weight on Home and
  // Settings (measured 18/09/2026: ~19 KB kept after leaving a book). They are
  // rebuilt by the next page prewarm, so hand them back here.
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();

  if (RECENT_BOOKS.hasUnsavedExcerpt()) {
    if (sleeping) {
      RECENT_BOOKS.saveExcerpt();
    } else {
      activityManager.deferWrite([] { RECENT_BOOKS.saveExcerpt(); });
    }
  }

  LOG_INF("MEM", "reader exit: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  if (!preview) {
    APP_STATE.readerActivityLoadCount = 0;
    if (sleeping) {
      APP_STATE.saveToFile();
    } else {
      activityManager.deferWrite([] { APP_STATE.saveToFile(); });
    }
  }

  endOfBookOptions.reset();
  endOfBookOptionsReady.store(false, std::memory_order_release);
}

void ReaderActivity::rememberExcerpt(const std::string& text) {
  RECENT_BOOKS.rememberExcerpt(bookPath, text);
}

void ReaderActivity::updateReadingTime(const bool active) {
  if (!statsEnabled) return;
  const uint32_t now = millis();
  const uint32_t elapsed = now - statsLastMs;
  const uint16_t turns = trangDaLat;
  trangDaLat = 0;
  if ((statsActive && elapsed) || turns) {
    READING_STATS.record(statsDay, statsActive ? elapsed : 0, turns, getScreenshotInfo().progressPercent);
    READING_STATS.observeHabits(statsActive ? elapsed : 0, turns, now);
    statsDirty = true;
  }
  // Attribute the elapsed interval to its starting day. Polling once a second
  // bounds the midnight transition error without using wall time as a duration.
  if (now - statsDayPollMs >= 1000) {
    statsDay = READING_STATS.currentDay();
    statsDayPollMs = now;
  }
  statsLastMs = now;
  statsActive = active;
}

void ReaderActivity::chotSoLieuDoc() {
  if (!statsEnabled || !statsDirty) return;
  if (READING_STATS.saveToFile()) statsDirty = false;
  statsSavedMs = millis();
}

void ReaderActivity::onTick() {
  // The first page is on the panel, or the end-of-book screen (both mark pageRendered; an error
  // screen does not, and commitOpen() waits for one of them).
  if (openCommitPending && pageRendered.load(std::memory_order_acquire)) {
    commitOpen();
  }
  // Never stall the input loop on a paint in flight: try-take instead of
  // blocking. A blocked main task stops gpio polling for the whole paint
  // (~2 s on X3), which silently eats short taps (the debounced press never
  // sees two consecutive agreeing samples).
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) return;
  updateReadingTime(pageReady.load(std::memory_order_acquire) && readingPageVisible());
  if (millis() - statsSavedMs >= 30000) chotSoLieuDoc();
}

void ReaderActivity::onPause() {
  pendingExternalTurn = 0;
#ifdef TENOR_TURN_TRACE
  dropTurnTrace(pendingExternalTurnTrace, "pause");
  const unsigned long started = millis();
#endif
  updateReadingTime(false);
  // Any other screen over the reader (the quote selector, the chapter list...) writes the
  // checkpoint after its own first frame instead of before it: 367 ms on the X3 in front of the
  // quote selector (r12). The record is in RAM until then, and the write runs as that screen
  // closes if it closes first (sleep included). A power cut in between loses the reading since the
  // last 30 s checkpoint, the bound a power cut on the page already has.
  if (!pauseKeepsStatsInRam && statsEnabled && statsDirty) {
    activityManager.deferWrite([] { READING_STATS.saveToFile(); });
    statsDirty = false;
    statsSavedMs = millis();
  }
  pauseKeepsStatsInRam = false;
#ifdef TENOR_TURN_TRACE
  LOG_INF("READER", "PAUSE_SAVE t=%lu ms=%lu", started, millis() - started);
#endif
}

void ReaderActivity::onResume() {
  statsLastMs = statsDayPollMs = millis();
  statsDay = READING_STATS.currentDay();
  statsActive = false;
}

bool ReaderActivity::handleBackNavigation() {
  if (!ReaderUtils::handleBackNavigation(mappedInput, activityManager)) return false;
  leaving.store(true, std::memory_order_release);
  return true;
}

void ReaderActivity::clearEndOfBookOptionsIfNeeded() {
  if (isAtEndOfBook() || !endOfBookOptionsReady.load(std::memory_order_acquire)) return;

  RenderLock lock(*this);
  endOfBookOptionsReady.store(false, std::memory_order_release);
  endOfBookOptions.reset();
}

bool ReaderActivity::endOfBookMenuActive() const {
  return isAtEndOfBook() && endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive();
}

bool ReaderActivity::handleEndOfBookMenu(const bool suppressConfirmRelease) {
  if (suppressConfirmRelease || !endOfBookMenuActive()) {
    return false;
  }

  std::string openPath;
  switch (endOfBookOptions->handleMenuInput(mappedInput, &openPath)) {
    case EndOfBookOptions::Action::OpenBook:
      activityManager.goToReader(openPath);
      return true;
    case EndOfBookOptions::Action::GoHome:
      onGoHome();
      return true;
    case EndOfBookOptions::Action::LastPage:
      onReturnFromEndOfBook();
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::Redraw:
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::None:
      return false;
  }

  return false;
}

bool ReaderActivity::handleEndOfBookPageTurn(const bool prevTriggered, const bool nextTriggered) {
  if (!isAtEndOfBook()) return false;

  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) {
    return true;
  }
  if (nextTriggered) {
    onGoHome();
  } else if (prevTriggered) {
    onReturnFromEndOfBook();
    requestUpdate();
  }
  return true;
}

void ReaderActivity::readingMargins(int& top, int& right, int& bottom, int& left) const {
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int margin = SETTINGS.screenMargin;
  if (tenorchrome::enabled()) {
    // Tenor reader margins are expressed in the renderer's current logical orientation.
    left = margin;
    // Keep the first line below the ink-safe floor, with a small visual breathing room.
    const int fontId = SETTINGS.getReaderFontId();
    int maxInkTop = 0;
    for (uint8_t style = EpdFontFamily::REGULAR; style <= EpdFontFamily::BOLD_ITALIC; ++style) {
      maxInkTop = std::max(maxInkTop, renderer.getFontMaxInkTop(fontId, static_cast<EpdFontFamily::Style>(style)));
    }
    constexpr int TOP_BREATHING_ROOM = 3;
    const int inkSafeTop =
        maxInkTop > 0 ? std::max(margin, maxInkTop - renderer.getFontAscenderSize(fontId)) : margin + 1;
    top = inkSafeTop + TOP_BREATHING_ROOM;
    right = margin;  // Keep the reader text inset equal on both sides.
    bottom = std::max(margin, preview                            ? static_cast<int>(PREVIEW_FOOTER_HEIGHT)
                              : SETTINGS.readerStatusBarHidden() ? 0
                                                                 : tenorchrome::readerBottomReserve());
    return;
  }
  top += margin;
  right += margin;
  left += margin;
  bottom += std::max(margin, static_cast<int>(readerStatusBarHeight()));
}

uint8_t ReaderActivity::readerStatusBarHeight() const {
  return preview ? PREVIEW_FOOTER_HEIGHT
                 : UITheme::getInstance().getStatusBarHeight(UITheme::StatusBarScope::Reader);
}

void ReaderActivity::drawPreviewFooter() const {
  const int y = renderer.getScreenHeight() - PREVIEW_FOOTER_HEIGHT;
  renderer.fillRect(0, y, renderer.getScreenWidth(), PREVIEW_FOOTER_HEIGHT, false);
  renderer.drawText(SMALL_FONT_ID, 12, y + 6, tr(STR_PREVIEW_HINT), true);
}

bool ReaderActivity::handlePreviewInput() {
  if (!preview) return false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activityManager.goToReader(bookPath);
    return true;
  }
  if (processExternalPageTurn()) return true;
  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  const auto turns = ReaderUtils::detectPageTurn(mappedInput);
  if (turns.prev || touch.prev) {
    if (isAtEndOfBook()) {
      RenderLock lock(RenderLock::TryTake{});
      if (lock.acquired()) {
        onReturnFromEndOfBook();
        requestUpdate();
      } else {
#ifdef TENOR_TURN_TRACE
        currentTurnTrace = detectTurnTrace("preview", false);
#endif
        queuePageTurn(false, true, "render_lock");
      }
    } else if (pageTurn(false)) {
      requestUpdate();
    }
  } else if (turns.next || touch.next) {
    if (!isAtEndOfBook() && pageTurn(true)) requestUpdate();
  }
  return true;
}

#ifdef TENOR_TURN_TRACE
ReaderActivity::TurnTrace ReaderActivity::detectTurnTrace(const char* source, const bool forward) {
  TurnTrace trace{++turnTraceSequence, millis(), source, forward};
  logTurnTrace("DETECTED", trace, "input");
  return trace;
}

void ReaderActivity::logTurnTrace(const char* phase, const TurnTrace& trace, const char* detail) const {
  if (trace.id == 0) return;
  const unsigned long now = millis();
  LOG_INF("RDR_TRACE", "%s id=%u gen=%u t=%lu age_ms=%lu src=%s dir=%u detail=%s heap=%u largest=%u min=%u",
          phase, static_cast<unsigned>(trace.id), static_cast<unsigned>(activityManager.activityGeneration()),
          now, now - trace.detectedMs, trace.source, trace.forward ? 1u : 0u, detail,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          static_cast<unsigned>(ESP.getMinFreeHeap()));
}

void ReaderActivity::replaceQueuedTurnTrace(TurnTrace& queue, const TurnTrace& incoming, const char* reason,
                                            const bool merged) {
  logTurnTrace(merged ? "MERGED" : "COALESCED", queue, reason);
  queue = incoming;
  logTurnTrace("QUEUED", queue, reason);
}

void ReaderActivity::dropTurnTrace(TurnTrace& trace, const char* reason) {
  logTurnTrace("DROPPED", trace, reason);
  trace = {};
}
#endif

bool ReaderActivity::pageTurnLocked(const bool isForward) {
#ifdef TENOR_TURN_TRACE
  if (currentTurnTrace.id == 0) currentTurnTrace = detectTurnTrace("local", isForward);
#endif
  if (!latTrangThat(isForward)) {
#ifdef TENOR_TURN_TRACE
    logTurnTrace("REJECTED", currentTurnTrace, "unchanged");
    currentTurnTrace = {};
#endif
    return false;
  }
#ifdef TENOR_TURN_TRACE
  logTurnTrace("SUPERSEDED", appliedTurnTrace, "next_mutation");
  appliedTurnTrace = currentTurnTrace;
  currentTurnTrace = {};
  logTurnTrace("APPLIED", appliedTurnTrace, "page");
#endif
  ++trangDaLat;
  return true;
}

bool ReaderActivity::pageTurn(const bool isForward) {
#ifdef TENOR_TURN_TRACE
  if (currentTurnTrace.id == 0) currentTurnTrace = detectTurnTrace("local", isForward);
#endif
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) {
    queuePageTurn(isForward, true, "render_lock");
    return false;
  }
  return pageTurnLocked(isForward);
}

void ReaderActivity::queuePageTurn(const bool isForward, const bool isLocal, const char* reason) {
  int queued = 0;
  if (isLocal && pendingTurnIsLocal && !pendingExternalChapter) queued = pendingExternalTurn;
  pendingExternalTurn =
      static_cast<int8_t>(std::clamp<int>(queued + (isForward ? 1 : -1), -MAX_QUEUED_TURNS, MAX_QUEUED_TURNS));
  pendingExternalChapter = false;
  pendingExternalGeneration = activityManager.activityGeneration();
  pendingTurnIsLocal = isLocal;
#ifdef TENOR_TURN_TRACE
  replaceQueuedTurnTrace(pendingExternalTurnTrace, currentTurnTrace, reason, queued != 0);
  currentTurnTrace = {};
  if (pendingExternalTurn == 0) dropTurnTrace(pendingExternalTurnTrace, "cancelled");
#else
  (void)reason;
#endif
}

bool ReaderActivity::luotLatTrangNgoai(const bool isForward) {
  if (!externalPageTurnAllowed()) return false;
  // The render task publishes this object once loaded. Menu state is changed
  // by the main input task, so reject its reports before resetting timers.
  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) return false;
  // One pending direction per reader generation. A repaint can take seconds;
  // repeated reports during that paint coalesce into the latest direction.
#ifdef TENOR_TURN_TRACE
  currentTurnTrace = detectTurnTrace("external", isForward);
#endif
  queuePageTurn(isForward, false, "external");
  return true;
}

bool ReaderActivity::luotNhayChuongNgoai(const bool isForward) {
  if (!externalPageTurnAllowed()) return false;
  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) return false;
  pendingExternalTurn = isForward ? 1 : -1;
  pendingExternalChapter = true;
  pendingExternalGeneration = activityManager.activityGeneration();
  pendingTurnIsLocal = false;
#ifdef TENOR_TURN_TRACE
  replaceQueuedTurnTrace(pendingExternalTurnTrace, detectTurnTrace("external", isForward), "external");
#endif
  return true;
}

bool ReaderActivity::processExternalPageTurn() {
  if (pendingExternalTurn == 0) return false;
  if (pendingExternalGeneration != activityManager.activityGeneration() ||
      (!pendingTurnIsLocal && !externalPageTurnAllowed())) {
    pendingExternalTurn = 0;
#ifdef TENOR_TURN_TRACE
    dropTurnTrace(pendingExternalTurnTrace, "stale_or_blocked");
#endif
    return false;
  }
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired() || !manualPageTurnReady() || pageAwaitsLayout()) return false;
  const bool forward = pendingExternalTurn > 0;
  const bool chapter = pendingExternalChapter;
  int8_t remaining = pendingExternalTurn;
  pendingExternalTurn = 0;
  pendingExternalChapter = false;
#ifdef TENOR_TURN_TRACE
  currentTurnTrace = pendingExternalTurnTrace;
  pendingExternalTurnTrace = {};
#endif
  // Preview owns Back/Confirm and stays inside its current book.
  if (preview && isAtEndOfBook()) {
#ifdef TENOR_TURN_TRACE
    dropTurnTrace(currentTurnTrace, "preview_end");
#endif
    if (!forward) {
      onReturnFromEndOfBook();
      requestUpdate();
    }
    return true;
  }
  // The same end-of-book gate owns physical and external page actions. An
  // open suggestion menu consumes the report without turning a hidden page.
  if (!preview && handleEndOfBookPageTurn(!forward, forward)) {
#ifdef TENOR_TURN_TRACE
    dropTurnTrace(currentTurnTrace, "end_of_book");
#endif
    return true;
  }
  if (chapter) {
    // Giu nut tren remote. Khong dem vao so trang da lat: mot nac chuong khong phai
    // mot trang doc. Sach khong co muc luc thi khong lam gi, chi ghi mot dong.
    if (nhayChuongThat(forward ? 1 : -1)) {
      requestUpdate();
    } else {
      LOG_INF("READER", "Hold ignored: this book has no chapters to skip");
    }
    return true;
  }
  // Presses queued during a paint land together: one repaint shows the page they add up to.
  bool changed = false;
  while (remaining != 0 && !isAtEndOfBook() && pageTurnLocked(forward)) {
    changed = true;
    remaining -= forward ? 1 : -1;
    if (pageAwaitsLayout()) break;
  }
  // A turn into a chapter, or onto a page, that is still being laid out stops early; the rest
  // waits for it.
  if (changed && !isAtEndOfBook()) pendingExternalTurn = remaining;
  if (changed) requestUpdate();
  return false;
}

// Back raises `leaving` and asks for Home; while that transition is pending the activity manager
// does not run this loop. It runs again only if the exit was dropped (a sleep that replaced it and
// then gave up): the reader stays, and a paint the flag cut short is painted again.
void ReaderActivity::stayAfterDroppedExit() {
  if (!leaving.exchange(false, std::memory_order_acq_rel)) return;
  requestUpdate();
}

void ReaderActivity::loop() {
  stayAfterDroppedExit();
  if (handlePreviewInput()) return;
  clearEndOfBookOptionsIfNeeded();
  if (handleEndOfBookMenu()) {
    pendingExternalTurn = 0;
#ifdef TENOR_TURN_TRACE
    dropTurnTrace(pendingExternalTurnTrace, "end_menu");
#endif
    return;
  }
  if (handleFormatInput()) return;
  if (handleBackNavigation()) return;
  if (processExternalPageTurn()) return;

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  const auto turns = ReaderUtils::detectPageTurn(mappedInput);
  const bool prevTriggered = turns.prev || turns.prevLongPressed || touch.prev;
  const bool nextTriggered = turns.next || turns.nextLongPressed || touch.next;
  if (!prevTriggered && !nextTriggered) return;
  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) return;

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool longPress =
      !turns.fromTilt &&
      (turns.prevLongPressed || turns.nextLongPressed ||
       ((touch.prev || touch.next) && heldMs >= ReaderUtils::SKIP_HOLD_MS));
  if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.FONT_SIZE_STEP) {
    if (docCoChuMotNac(nextTriggered ? 1 : -1)) requestUpdate();
    return;
  }
  const bool skip = longPress && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP;

  const bool changed = skip ? skipPages(prevTriggered ? -10 : 10) : pageTurn(!prevTriggered);
  if (changed) requestUpdate();
}

void ReaderActivity::render(RenderLock&&) {
  if (isAtEndOfBook()) {
    if (preview) {
      renderer.clearScreen();
      drawPreviewFooter();
      renderer.displayBuffer();
#ifdef TENOR_TURN_TRACE
      logTurnTrace("RENDERED", appliedTurnTrace, "preview_end");
      appliedTurnTrace = {};
#endif
      return;
    }
    if (!endOfBookOptions) {
      endOfBookOptions = makeUniqueNoThrow<EndOfBookOptions>(renderer);
      if (!endOfBookOptions) LOG_ERR("READER", "OOM: EndOfBookOptions");
    }
    renderer.clearScreen();
    if (endOfBookOptions) {
      endOfBookOptions->loadOnce(bookPath);
      // Release-publish AFTER loadOnce() so the main task's acquire load can't
      // observe an object whose names/selector are still being populated.
      endOfBookOptionsReady.store(true, std::memory_order_release);
      endOfBookOptions->render(renderer, mappedInput);
    }
    renderer.displayBuffer();
#ifdef TENOR_TURN_TRACE
    logTurnTrace("RENDERED", appliedTurnTrace, "end_of_book");
    appliedTurnTrace = {};
#endif
    onEndOfBookRendered();
    markPageRendered();
    return;
  }

  renderBook();
  pageReady.store(true, std::memory_order_release);
}

bool ReaderActivity::handleForcedRefresh() {
  {
    RenderLock lock(*this);
    pagesUntilFullRefresh = 1;
    forcedRefreshPending = true;
  }
  requestUpdate();
  return true;
}
