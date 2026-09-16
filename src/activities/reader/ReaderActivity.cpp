#include "ReaderActivity.h"

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

  updateReadingTime(false);
  chotSoLieuDoc();

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
  RenderLock lock;
  updateReadingTime(pageReady.load(std::memory_order_acquire) && readingPageVisible());
  if (millis() - statsSavedMs >= 30000) chotSoLieuDoc();
}

void ReaderActivity::onPause() {
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
    // tenor/cross uses the physical page edges, matching the fixed footer.
    // Default text inset is 5 px; preserve an explicitly larger reader margin.
    left = margin;
    top = margin + 1;    // Vietnamese accents can exceed the font ascender by one pixel.
    right = margin + 3;  // Reserve ink overhang beyond the final glyph advance.
    bottom = std::max(margin, preview                            ? static_cast<int>(PREVIEW_FOOTER_HEIGHT)
                              : SETTINGS.readerStatusBarHidden() ? 0
                                                                 : 28);
    return;
  }
  top += margin;
  right += margin;
  left += margin;
  bottom += std::max(margin, static_cast<int>(readerStatusBarHeight()));
}

uint8_t ReaderActivity::readerStatusBarHeight() const {
  return preview ? PREVIEW_FOOTER_HEIGHT : UITheme::getInstance().getStatusBarHeight();
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
  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  const auto turns = ReaderUtils::detectPageTurn(mappedInput);
  if (turns.prev || touch.prev) {
    if (isAtEndOfBook())
      onReturnFromEndOfBook();
    else
      pageTurn(false);
    requestUpdate();
  } else if (turns.next || touch.next) {
    if (!isAtEndOfBook()) pageTurn(true);
    requestUpdate();
  }
  return true;
}

void ReaderActivity::loop() {
  if (handlePreviewInput()) return;
  clearEndOfBookOptionsIfNeeded();
  if (handleEndOfBookMenu()) return;
  if (handleFormatInput()) return;
  if (handleBackNavigation()) return;

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;
  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) return;

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  if (!fromTilt && SETTINGS.longPressButtonBehavior == SETTINGS.FONT_SIZE_STEP && heldMs >= ReaderUtils::SKIP_HOLD_MS) {
    if (docCoChuMotNac(nextTriggered ? 1 : -1)) requestUpdate();
    return;
  }
  const bool skip =
      !fromTilt && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP && heldMs >= ReaderUtils::SKIP_HOLD_MS;

  if (prevTriggered) {
    if (skip) {
      skipPages(-10);
    } else {
      pageTurn(false);
    }
  } else {
    if (skip) {
      skipPages(10);
    } else {
      pageTurn(true);
    }
  }
  requestUpdate();
}

void ReaderActivity::render(RenderLock&&) {
  if (isAtEndOfBook()) {
    if (preview) {
      renderer.clearScreen();
      drawPreviewFooter();
      renderer.displayBuffer();
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
