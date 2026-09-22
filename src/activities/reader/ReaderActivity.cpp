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

  if (!Storage.exists(bookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
    finish();
    return;
  }

  trangDaLat = 0;

  sdFontSystem.ensureLoaded(renderer);
  applyInitialOrientation();

  if (!loadBook()) {
    finish();
    return;
  }

  if (preview) {
    LOG_INF("READER", "Preview: %s", bookPath.c_str());
    requestUpdate();
    return;
  }

  statsEnabled = READING_STATS.activateBook(bookPath, getScreenshotInfo().progressPercent, getBookTitle());
  statsLastMs = statsSavedMs = statsDayPollMs = millis();
  statsDay = READING_STATS.currentDay();

  APP_STATE.openEpubPath = bookPath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath());
  requestUpdate();
}

void ReaderActivity::onExit() {
  Activity::onExit();
  pendingExternalTurn = 0;
#ifdef TENOR_UI_ACCEPTANCE
  dropTurnTrace(pendingExternalTurnTrace, "exit");
#endif

  updateReadingTime(false);
  chotSoLieuDoc();
  // The SD font glyph arenas built while reading are dead weight on Home and
  // Settings (measured 18/09/2026: ~19 KB kept after leaving a book). They are
  // rebuilt by the next page prewarm, so hand them back here.
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  if (!preview) {
    APP_STATE.readerActivityLoadCount = 0;
    APP_STATE.saveToFile();
  }

  endOfBookOptions.reset();
  endOfBookOptionsReady.store(false, std::memory_order_release);
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
#ifdef TENOR_UI_ACCEPTANCE
  dropTurnTrace(pendingExternalTurnTrace, "pause");
#endif
  updateReadingTime(false);
  chotSoLieuDoc();
}

void ReaderActivity::onResume() {
  statsLastMs = statsDayPollMs = millis();
  statsDay = READING_STATS.currentDay();
  statsActive = false;
}

bool ReaderActivity::handleBackNavigation() { return ReaderUtils::handleBackNavigation(mappedInput, activityManager); }

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
        pendingExternalTurn = -1;
        pendingExternalGeneration = activityManager.activityGeneration();
        pendingTurnIsLocal = true;
#ifdef TENOR_UI_ACCEPTANCE
        replaceQueuedTurnTrace(pendingExternalTurnTrace, detectTurnTrace("preview", false), "render_lock");
#endif
      }
    } else if (pageTurn(false)) {
      requestUpdate();
    }
  } else if (turns.next || touch.next) {
    if (!isAtEndOfBook() && pageTurn(true)) requestUpdate();
  }
  return true;
}

#ifdef TENOR_UI_ACCEPTANCE
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

void ReaderActivity::replaceQueuedTurnTrace(TurnTrace& queue, const TurnTrace& incoming, const char* reason) {
  logTurnTrace("COALESCED", queue, reason);
  queue = incoming;
  logTurnTrace("QUEUED", queue, reason);
}

void ReaderActivity::dropTurnTrace(TurnTrace& trace, const char* reason) {
  logTurnTrace("DROPPED", trace, reason);
  trace = {};
}
#endif

bool ReaderActivity::pageTurnLocked(const bool isForward) {
#ifdef TENOR_UI_ACCEPTANCE
  if (currentTurnTrace.id == 0) currentTurnTrace = detectTurnTrace("local", isForward);
#endif
  if (!latTrangThat(isForward)) {
#ifdef TENOR_UI_ACCEPTANCE
    logTurnTrace("REJECTED", currentTurnTrace, "unchanged");
    currentTurnTrace = {};
#endif
    return false;
  }
#ifdef TENOR_UI_ACCEPTANCE
  logTurnTrace("SUPERSEDED", appliedTurnTrace, "next_mutation");
  appliedTurnTrace = currentTurnTrace;
  currentTurnTrace = {};
  logTurnTrace("APPLIED", appliedTurnTrace, "page");
#endif
  ++trangDaLat;
  return true;
}

bool ReaderActivity::pageTurn(const bool isForward) {
#ifdef TENOR_UI_ACCEPTANCE
  if (currentTurnTrace.id == 0) currentTurnTrace = detectTurnTrace("local", isForward);
#endif
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) {
    pendingExternalTurn = isForward ? 1 : -1;
    pendingExternalGeneration = activityManager.activityGeneration();
    pendingTurnIsLocal = true;
#ifdef TENOR_UI_ACCEPTANCE
    replaceQueuedTurnTrace(pendingExternalTurnTrace, currentTurnTrace, "render_lock");
    currentTurnTrace = {};
#endif
    return false;
  }
  return pageTurnLocked(isForward);
}

bool ReaderActivity::luotLatTrangNgoai(const bool isForward) {
  if (!externalPageTurnAllowed()) return false;
  // The render task publishes this object once loaded. Menu state is changed
  // by the main input task, so reject its reports before resetting timers.
  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) return false;
  // One pending direction per reader generation. A repaint can take seconds;
  // repeated reports during that paint coalesce into the latest direction.
  pendingExternalTurn = isForward ? 1 : -1;
  pendingExternalGeneration = activityManager.activityGeneration();
  pendingTurnIsLocal = false;
#ifdef TENOR_UI_ACCEPTANCE
  replaceQueuedTurnTrace(pendingExternalTurnTrace, detectTurnTrace("external", isForward), "external");
#endif
  return true;
}

bool ReaderActivity::processExternalPageTurn() {
  if (pendingExternalTurn == 0) return false;
  if (pendingExternalGeneration != activityManager.activityGeneration() ||
      (!pendingTurnIsLocal && !externalPageTurnAllowed())) {
    pendingExternalTurn = 0;
#ifdef TENOR_UI_ACCEPTANCE
    dropTurnTrace(pendingExternalTurnTrace, "stale_or_blocked");
#endif
    return false;
  }
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired() || !manualPageTurnReady()) return true;
  const bool forward = pendingExternalTurn > 0;
  pendingExternalTurn = 0;
#ifdef TENOR_UI_ACCEPTANCE
  currentTurnTrace = pendingExternalTurnTrace;
  pendingExternalTurnTrace = {};
#endif
  // Preview owns Back/Confirm and stays inside its current book.
  if (preview && isAtEndOfBook()) {
#ifdef TENOR_UI_ACCEPTANCE
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
#ifdef TENOR_UI_ACCEPTANCE
    dropTurnTrace(currentTurnTrace, "end_of_book");
#endif
    return true;
  }
  if (pageTurnLocked(forward)) requestUpdate();
  return true;
}

void ReaderActivity::loop() {
  if (handlePreviewInput()) return;
  clearEndOfBookOptionsIfNeeded();
  if (handleEndOfBookMenu()) {
    pendingExternalTurn = 0;
#ifdef TENOR_UI_ACCEPTANCE
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
#ifdef TENOR_UI_ACCEPTANCE
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
#ifdef TENOR_UI_ACCEPTANCE
    logTurnTrace("RENDERED", appliedTurnTrace, "end_of_book");
    appliedTurnTrace = {};
#endif
    onEndOfBookRendered();
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
