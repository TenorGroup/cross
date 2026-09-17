#include "EpubReaderChapterSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <string>

#include "MappedInputManager.h"
#include "activities/util/ChapterNumberEntryActivity.h"
#include "components/TenorMenuChrome.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "util/ChapterNumber.h"

namespace fui = freeink::ui;

EpubReaderChapterSelectionActivity::EpubReaderChapterSelectionActivity(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput,
                                                                       const std::shared_ptr<Epub>& epub,
                                                                       const int currentSpineIndex)
    : UiListActivity("EpubReaderChapterSelection", renderer, mappedInput),
      epub(epub),
      currentSpineIndex(currentSpineIndex) {}

void EpubReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();

  // The reader underneath pins its page-render glyph arenas while this
  // overlay is up. clearCache() is heap-adaptive: below the retention floor
  // it frees them (the next page render's PrewarmScope rebuilds them at
  // normal page-turn cost), giving this list room to keep every row's
  // fallback glyphs resident - otherwise each repaint re-reads the visible
  // rows' glyphs from SD.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }

  if (!epub) {
    return;
  }

  // Start with the current chapter at the top of the viewport; the first
  // screen build pulls the viewport to it (ListNav follow-on-build) and
  // materializes the row window there (refreshTocWindow in buildScreen).
  int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex == -1) {
    tocIndex = 0;
  }
  nav.selected = tocIndex;
}

// Materialize the ListItem/label window starting at `start` (clamped). TOC
// entries are SD LUT reads (getTocItem), so this runs only when the viewport
// leaves the current window. Finishes with a batch prewarm of the window's
// CJK fallback glyphs -- one bounded SD pass per list page; repaints inside
// the window stay RAM-only.
void EpubReaderChapterSelectionActivity::refreshTocWindow(const int start) {
  const int total = listCount();
  int clamped = start;
  if (clamped > total - TOC_WINDOW) clamped = total - TOC_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == windowStart) return;

  windowCount = total - clamped < TOC_WINDOW ? total - clamped : TOC_WINDOW;
  for (int i = 0; i < windowCount; i++) {
    if (filterNumber && clamped + i == matchCount) {
      windowLabels[i] = tr(STR_CHAPTER_MORE_RESULTS);
      freeink::ui::ListItem item;
      item.label = windowLabels[i].c_str();
      item.actionValue = static_cast<int16_t>(clamped + i);
      windowItems[i] = item;
      continue;
    }
    const auto tocItem = epub->getTocItem(filterNumber ? matches[clamped + i] : clamped + i);
    std::string indent(tocItem.level > 0 ? (tocItem.level - 1) * 2 : 0, ' ');
    windowLabels[i] = indent + tocItem.title;
    fui::ListItem item;
    item.label = windowLabels[i].c_str();
    item.actionValue = static_cast<int16_t>(clamped + i);
    windowItems[i] = item;
  }
  windowStart = clamped;

  struct PrewarmCtx {
    const std::string* labels;
    int count;
  } prewarmCtx{windowLabels, windowCount};
  renderer.prewarmFallbackText(
      uiScaleSpec().bodyFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        const auto* c = static_cast<const PrewarmCtx*>(ctx);
        return i < static_cast<uint32_t>(c->count) ? c->labels[i].c_str() : nullptr;
      },
      &prewarmCtx, static_cast<uint32_t>(windowCount));
}

void EpubReaderChapterSelectionActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) {
    return;
  }
  // The activated row leaves this screen (finish); a lingering flash would gray
  // an unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  if (filterNumber && index == matchCount) {
    startSearch(true);
    return;
  }
  const auto tocItem = epub->getTocItem(filterNumber ? matches[index] : index);
  if (tocItem.spineIndex == -1) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  } else {
    setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
    finish();
  }
}

bool EpubReaderChapterSelectionActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (filterNumber) {
      {
        RenderLock lock(*this);
        filterNumber = 0;
        searching = false;
        searchFailed = false;
        searchCursor.reset();
        moreMatches = false;
        nav.selected = originalSelection;
        nav.followOnBuild = true;
        windowStart = -1;
      }
      requestUpdate();
      return true;
    }
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }

  if (!epub) {
    return true;
  }

  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 700)) {
    if (!searching && epub->getTocItemsCount() > 0) enterNumber();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void EpubReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false, UITheme::StatusBarScope::Reader);
  // Content: the safe area minus the header band drawChrome paints the title in.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (!epub) {
    return;
  }
  if (listCount() == 0) {
    screen.centeredText(I18N.get(searchFailed   ? StrId::STR_ERROR_GENERAL_FAILURE
                                 : searching    ? StrId::STR_CHAPTER_SEARCHING
                                 : filterNumber ? StrId::STR_CHAPTER_NOT_FOUND
                                                : StrId::STR_NO_CHAPTERS),
                        screen.theme().bodyText);
    return;
  }

  if (!SETTINGS.globalStatusBarHidden()) {
    const int bottom = screen.body().y + screen.body().height;
    const int tipTop = tenorchrome::tipY(renderer) - 2;
    if (bottom > tipTop) screen.takeBottom(static_cast<int16_t>(bottom - tipTop));
  }
  fui::ListProps props;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  // Materialize the row window for the final viewport (syncListViewport just
  // applied follow/clamping to nav.top) and hand list() the window with its
  // absolute base index.
  refreshTocWindow(nav.top);
  props.items = windowItems;
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart);
  screen.list(props);
}

void EpubReaderChapterSelectionActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false, UITheme::StatusBarScope::Reader);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER));
}

void EpubReaderChapterSelectionActivity::enterNumber() {
  const uint32_t initial =
      filterNumber ? filterNumber : chapterNumber::parse(epub->getTocItem(nav.selected).title.c_str());
  startActivityForResult(makeUniqueNoThrow<ChapterNumberEntryActivity>(renderer, mappedInput, initial ? initial : 1),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           {
                             RenderLock lock(*this);
                             if (!filterNumber) originalSelection = nav.selected;
                             filterNumber = std::get<IntervalResult>(result.data).value;
                           }
                           startSearch();
                         });
}
void EpubReaderChapterSelectionActivity::startSearch(bool nextBatch) {
  {
    RenderLock lock(*this);
    if (!nextBatch) scanIndex = 0;
    matchCount = 0;
    moreMatches = false;
    searchCursor = epub->openTocCursor(scanIndex);
    searchFailed = !searchCursor;
    searching = !searchFailed;
    searchStarted = millis();
    LOG_DBG("CHSEARCH", "start number=%lu index=%d total=%d", static_cast<unsigned long>(filterNumber), scanIndex,
            epub->getTocItemsCount());
#ifdef TENOR_UI_ACCEPTANCE
    LOG_INF("CHSEARCH", "start heap=%u largest=%u min=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
            ESP.getMinFreeHeap());
#endif
    windowStart = -1;
    nav.selected = 0;
    nav.top = 0;
    nav.followOnBuild = true;
  }
  requestUpdate();
}
void EpubReaderChapterSelectionActivity::loop() {
  // Bounded work keeps Back responsive; only indices are retained while scanning.
  if (searching) {
    bool done = false;
    {
      RenderLock lock(*this);
      const uint32_t started = millis();
      for (int n = 0; n < 64 && scanIndex < epub->getTocItemsCount(); ++n) {
        BookMetadataCache::TocEntry item;
        if (!searchCursor->next(item)) {
          searching = false;
          searchFailed = true;
          matchCount = 0;
          moreMatches = false;
          done = true;
          break;
        }
        if (item.spineIndex >= 0 && chapterNumber::parse(item.title.c_str()) == filterNumber) {
          if (matchCount == TOC_WINDOW) {
            moreMatches = true;
            searching = false;
            done = true;
            break;  // Leave this match for the next batch.
          }
          matches[matchCount++] = scanIndex;
        }
        ++scanIndex;
        if (millis() - started >= 8) break;
      }
      if (scanIndex == epub->getTocItemsCount()) {
        searching = false;
        done = true;
      }
    }
    if (done) {
#ifdef TENOR_UI_ACCEPTANCE
      LOG_INF("CHSEARCH", "end number=%lu ms=%lu scanned=%d matches=%d heap=%u largest=%u min=%u",
              static_cast<unsigned long>(filterNumber), static_cast<unsigned long>(millis() - searchStarted), scanIndex,
              matchCount, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
#endif
      searchCursor.reset();
#ifdef TENOR_UI_ACCEPTANCE
      LOG_INF("CHSEARCH", "released heap=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
      LOG_DBG("CHSEARCH", "done elapsed=%lu scanned=%d matches=%d",
              static_cast<unsigned long>(millis() - searchStarted), scanIndex, matchCount);
      requestUpdate();
    } else {
      delay(1);  // Let the idle task run while main bypasses its normal polling delay.
    }
  }
  UiListActivity::loop();
}
void EpubReaderChapterSelectionActivity::drawFooter() {
  tenorchrome::drawTip(renderer, tr(STR_CHAPTER_NUMBER_HINT));
  UiListActivity::drawFooter();
}
