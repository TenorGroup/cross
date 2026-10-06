#include "EpubReaderActivity.h"

#include <BlePageTurner.h>
#include <Epub/BuildStageProbe.h>
#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <GrayThumb.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>

#include "../../util/BookmarkFile.h"
#include "../../util/CoverRef.h"
#include "BookmarkEntry.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnoteSelectActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "HeapMapProbe.h"
#include "MappedInputManager.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "QuoteHighlight.h"
#include "ReaderActivity.h"
#include "ReaderFontChon.h"
#include "ReaderFontSizes.h"
#include "ReaderToolbarUi.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "activities/home/QuotesActivity.h"
#include "activities/settings/BlePageTurnerActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "components/HomeExcerptStyle.h"
#include "components/ReaderTapTip.h"
#include "ReaderMenuLayout.h"
#include "components/TenorMenuChrome.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "shells/Shell.h"
#include "shells/ugly/UglyInk.h"
#include "shells/ugly/UglyLogic.h"
#include "util/BookmarkUtil.h"
#include "util/ButtonNavigator.h"
#include "util/ReadingExcerpt.h"
#include "util/ScreenshotUtil.h"

// Where a jump to another place in the book began; the paint that lands it logs JUMP_BUILD and,
// once readable, READABLE_BOUND. Press-probe builds only.
#ifdef TENOR_PRESS_PROBE
#define TRACE_JUMP_BEGIN(source)                                                                                  \
  do {                                                                                                            \
    LOG_INF("ERS", "JUMP_BEGIN src=%s spine=%d t=%lu", source, currentSpineIndex,                                 \
            static_cast<unsigned long>(millis()));                                                                \
    buildprobe::reset();                                                                                          \
  } while (0)
#else
#define TRACE_JUMP_BEGIN(source)
#endif

namespace {
// Anh chup cac cai dat lam thay doi cach dan trang. So truoc va sau khi mo man Cai dat van ban
// de biet co phai dan lai hay khong.
struct AnhChupChu {
  uint8_t fontFamily, fontPointSize, lineSpacing, screenMargin, paragraphAlignment, extraParagraphSpacing,
      paragraphIndent, dropCapMode, hyphenationEnabled, embeddedStyle, textAntiAliasing, readerInkWeight,
      letterSpacing, wordSpacing, readerStatusBarMode, globalStatusBarMode;
  std::string sdFontFamilyName;
  static AnhChupChu chup() {
    return {SETTINGS.fontFamily,          SETTINGS.fontPointSize,
            SETTINGS.lineSpacing,         SETTINGS.screenMargin,
            SETTINGS.paragraphAlignment,  SETTINGS.extraParagraphSpacing,
            SETTINGS.paragraphIndent,     SETTINGS.dropCapMode,
            SETTINGS.hyphenationEnabled,  SETTINGS.embeddedStyle,
            SETTINGS.textAntiAliasing,    SETTINGS.readerInkWeight,
            SETTINGS.letterSpacing,       SETTINGS.wordSpacing,
            SETTINGS.readerStatusBarMode, SETTINGS.globalStatusBarMode,
            std::string(SETTINGS.sdFontFamilyName)};
  }
  bool operator==(const AnhChupChu& o) const {
    return fontFamily == o.fontFamily && fontPointSize == o.fontPointSize && lineSpacing == o.lineSpacing &&
           screenMargin == o.screenMargin && paragraphAlignment == o.paragraphAlignment &&
           extraParagraphSpacing == o.extraParagraphSpacing && paragraphIndent == o.paragraphIndent &&
           dropCapMode == o.dropCapMode && hyphenationEnabled == o.hyphenationEnabled &&
           embeddedStyle == o.embeddedStyle && textAntiAliasing == o.textAntiAliasing &&
           sdFontFamilyName == o.sdFontFamilyName && readerInkWeight == o.readerInkWeight &&
           letterSpacing == o.letterSpacing && wordSpacing == o.wordSpacing &&
           readerStatusBarMode == o.readerStatusBarMode && globalStatusBarMode == o.globalStatusBarMode;
  }
};
// The X4 Pro and X4 Classic carry the X4's panel but sit outside isXteinkDevice()
// (that helper also gates power management). Overlay refresh choices are per-panel:
// this family runs the grayscale anti-aliasing pass, so chrome painted over a
// fresh page needs the HALF ghost-cleanup and closing re-renders the page.
bool xteinkClassPanel() { return gpio.isXteinkDevice() || BoardConfig::isX4Pro() || BoardConfig::isX4Classic(); }

constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
constexpr size_t initialBookmarkCacheCapacity = 16;
constexpr float bookmarkProgressEpsilon = 0.0001f;

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

constexpr char READ_FOLDER[] = "/read";

bool isInReadFolder(const std::string& path) {
  constexpr size_t n = sizeof(READ_FOLDER) - 1;
  return path.size() > n && path.compare(0, n, READ_FOLDER) == 0 && path[n] == '/';
}

struct ProgressRange {
  float start;
  float end;
};

ProgressRange getPageProgressRange(const std::shared_ptr<Epub>& epub, const int spineIndex, const int page,
                                   const int pageCount) {
  if (pageCount <= 1) {
    return {epub->calculateProgress(spineIndex, 0.0f), epub->calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount - 1);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub->calculateProgress(spineIndex, start), epub->calculateProgress(spineIndex, end)};
}

bool bookmarkMatchesProgress(const BookmarkEntry& bookmark, const int spineIndex, const int page, const int pageCount,
                             const ProgressRange& pageRange) {
  if (bookmark.computedSpineIndex == spineIndex && bookmark.computedChapterPageCount == pageCount &&
      bookmark.computedChapterProgress == page) {
    return true;
  }

  const float bookmarkProgress = std::clamp(bookmark.percentage, 0.0f, 1.0f);
  return bookmarkProgress + bookmarkProgressEpsilon >= pageRange.start &&
         bookmarkProgress - bookmarkProgressEpsilon <= pageRange.end;
}

std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  Storage.mkdir(READ_FOLDER);
  std::string dstPath = std::string(READ_FOLDER) + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = std::string(READ_FOLDER) + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

void moveFinishedBookToReadFolder(const std::string& srcPath, const std::string& dstPath,
                                  const std::string& oldCachePath) {
  LOG_INF("ERS", "Moving finished epub: %s -> %s", srcPath.c_str(), dstPath.c_str());
  if (!Storage.rename(srcPath.c_str(), dstPath.c_str())) {
    LOG_ERR("ERS", "Failed to move finished book to '/Read' folder");
    return;
  }

  const std::string newCachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(dstPath));
  if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
    if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
      LOG_ERR("ERS", "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(), newCachePath.c_str());
    }
  }

  RECENT_BOOKS.updatePath(srcPath, dstPath, oldCachePath, newCachePath);
  if (APP_STATE.openEpubPath == srcPath) {
    APP_STATE.openEpubPath = dstPath;
    APP_STATE.saveToFile();
  }
}

// The missing cover thumbnails of a new book, built in RAM while its cover page decodes the cover
// (ImageBlock::ThumbHook) and written once that page is on the panel. The X3 otherwise copied the
// cover out of the book again and decoded it twice more when the reader closed: 5,6 s on the Home
// key (r18-k1). Both heights are built from the decode (GrayThumb::alsoFeed); when the heap under
// the page has room for the card's alone, the theme's is scaled from it when the files are
// written. One try per book open; whatever is not written here is written by writePendingThumbs()
// as before.
class CoverThumbCapture final : public ImageBlock::ThumbHook {
  const Epub& epub;
  const int* heights;
  int count;
  std::unique_ptr<GrayThumb> thumb, small;
  bool ready = false;

 public:
  CoverThumbCapture(const Epub& epub, const int* heights, const int count)
      : epub(epub), heights(heights), count(count) {}

  GrayThumb* open(const std::string& srcPath) override {
    if (count == 0 || thumb || !FsHelpers::hasJpgExtension(srcPath) || !epub.isCoverImage(srcPath)) return nullptr;
    thumb = makeUniqueNoThrow<GrayThumb>(heights[0]);
    if (thumb && count > 1 && heights[1] > 0 && heights[1] < heights[0]) {
      small = makeUniqueNoThrow<GrayThumb>(heights[1]);
      thumb->alsoFeed(small.get());
    }
    return thumb.get();
  }

  void close(const bool decoded) override {
    ready = decoded && thumb && thumb->finish();
    if (!ready) {
      thumb.reset();
      small.reset();
    }
    count = 0;
  }

  // Writes what the decode built. True when every missing height is on the card now.
  bool write() {
    if (!ready) return false;
    ready = false;
    const unsigned long started = millis();
    int written = 0;
    for (int i = 0; i < 2 && heights[i] > 0 && (i == 0 || heights[1] < heights[0]); i++) {
      const auto path = epub.getThumbBmpPath(heights[i]);
      // Under a temporary name until whole, as Epub::generateThumbBmps writes it: the reader
      // only asks whether the name exists, so a cut write must not leave it behind.
      const auto part = path + ".tmp";
      Storage.remove(part.c_str());
      HalFile file;
      bool ok = Storage.openFileForWrite("ERS", part, file) &&
                (i == 0                    ? thumb->writeTo(file)
                 : small && small->ready() ? small->writeTo(file)
                                           : thumb->writeScaled(heights[i], file));
      file.close();
      ok = ok && Storage.rename(part.c_str(), path.c_str());
      if (!ok) {
        Storage.remove(part.c_str());
        break;
      }
      LOG_INF("ERS", "Cover thumbnail %d px: %lu ms, ok=1, page=1", heights[i], millis() - started);
      written++;
    }
    thumb.reset();
    small.reset();
    return written > 0 && (written == 2 || heights[1] == 0);
  }
};

}  // namespace

EpubReaderActivity::~EpubReaderActivity() {
  ImageBlock::setExtractor(nullptr, nullptr);
  ImageBlock::setThumbHook(nullptr);
  // ActivityManager destroys activities with its RenderLock already held;
  // taking another here self-deadlocks (renderingMutex is non-recursive).
  settleOverlayRefresh();
  discardOverlayPage();  // free the overlay's page snapshot if one is held

  if (epub) saveLinkStack();

  section.reset();
  if (pendingReadFolderMove && epub) {
    const std::string srcPath = epub->getPath();
    const std::string oldCachePath = epub->getCachePath();
    const std::string dstPath = buildReadFolderDestination(srcPath);
    epub.reset();
    moveFinishedBookToReadFolder(srcPath, dstPath, oldCachePath);
  } else {
    epub.reset();
  }
}

bool EpubReaderActivity::loadBook() {
#ifdef TENOR_TURN_TRACE
  const unsigned long loadStarted = millis();
#endif
  auto loadedEpub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
  if (!loadedEpub) {
    LOG_ERR("ERS", "Failed to allocate EPUB object free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    return false;
  }

  const bool uncached = !BookMetadataCache::indexOnCard(loadedEpub->getCachePath());
  if (uncached) {
    disableFastInitialRefresh();
#ifdef TENOR_PRESS_PROBE
    LOG_INF("ERS", "BUILD_POPUP src=book");
#endif
    GUI.drawPopup(renderer, tr(STR_INDEXING));
  }

  bool loaded;
  {
#ifdef TENOR_UI_ACCEPTANCE
    if (uncached) {
      LOG_DBG("ERS", "EPUB_LOAD stage=before free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(ESP.getMaxAllocHeap()));
    }
#endif
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    if (uncached) loan.emplace(renderer);
    loaded = loadedEpub->load(true, SETTINGS.embeddedStyle == 0);
  }
#ifdef TENOR_UI_ACCEPTANCE
  if (uncached) {
    LOG_DBG("ERS", "EPUB_LOAD stage=after loaded=%u free=%u largest=%u", loaded ? 1u : 0u,
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
#endif
  if (!loaded) {
    LOG_ERR("ERS", "Failed to load EPUB free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    return false;
  }
  epub = std::move(loadedEpub);
  tocSpineCached = -1;  // sach khac: bo cache khoang muc TOC cua spine

  ImageBlock::clearRenderFailures();
  ImageBlock::setExtractor(epub.get(), [](void* ctx, const char* src, const char* dest) {
    return static_cast<Epub*>(ctx)->extractItemToFile(src, dest);
  });

#ifdef TENOR_TURN_TRACE
  const unsigned long epubLoaded = millis();
#endif
  epub->setupCacheDir();

  HalFile f;
  if (!preview && Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[10];
    int dataSize = f.read(data, sizeof(data));
    if (dataSize == 4 || dataSize == 6 || dataSize == 10) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      if (nextPageNumber == UINT16_MAX) {
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    } else if (dataSize == 10) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
      cachedVisibleTextOffset = static_cast<uint32_t>(data[6]) | (static_cast<uint32_t>(data[7]) << 8) |
                                (static_cast<uint32_t>(data[8]) << 16) | (static_cast<uint32_t>(data[9]) << 24);
    }
  }

  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      cachedVisibleTextOffset.reset();
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

#ifdef TENOR_TURN_TRACE
  const unsigned long progressRead = millis();
#endif
  if (!preview) loadLinkStack();
  loadCachedBookmarks();
#ifdef TENOR_TURN_TRACE
  const unsigned long bookmarksRead = millis();
#endif
  // Anchors of the quotes saved in this book, read once here so a page turn never
  // walks the quote directory. Preview never draws highlights, so it never pays.
  if (!preview) quotes::loadAnchors(bookPath, quoteAnchors);
#ifdef TENOR_TURN_TRACE
  const unsigned long quotesRead = millis();
#endif

  // The GAN DAY card only ever READS a thumbnail bitmap; nothing in the app generated one, so a
  // book added by this firmware always fell back to the brand placeholder (a device that showed a
  // real cover only did so for a thumbnail written by an older release). Generate it once per
  // book. The JPEG->1-bit BMP pass costs seconds and only Home reads its output, so only record
  // the miss here; onExit() writes it as the reader closes (see writePendingThumbs).
  // The tenor card draws the cover at its own height, well above the theme's, and scaling the
  // theme's thumbnail up that far makes the dither coarse, so that card gets a thumbnail of its
  // own, written first. The theme's stays: other themes read it, and the card falls back to it.
  // A book opened before the card's existed has the theme's alone and gets the card's here.
  if (!preview) {
    pendingThumbCount = 0;
    for (const int height : {tenorchrome::enabled() ? HOME_CARD_COVER_H : 0,
                             UITheme::getInstance().getMetrics().homeCoverHeight}) {
      if (height > 0 && !Storage.exists(epub->getThumbBmpPath(height).c_str()))
        pendingThumbHeights[pendingThumbCount++] = height;
    }
    if (pendingThumbCount > 0) {
      if (pendingThumbCount < 2) pendingThumbHeights[1] = 0;
      coverThumbs = makeUniqueNoThrow<CoverThumbCapture>(*epub, pendingThumbHeights, pendingThumbCount);
      ImageBlock::setThumbHook(coverThumbs.get());
      coverRefPending = true;
    }
  }
#ifdef TENOR_TURN_TRACE
  LOG_INF("ERS", "LOAD_STAGES epub=%lu progress=%lu marks=%lu quotes=%lu thumbs=%lu uncached=%u",
          epubLoaded - loadStarted, progressRead - epubLoaded, bookmarksRead - progressRead,
          quotesRead - bookmarksRead, millis() - quotesRead, uncached ? 1u : 0u);
#endif
  // Touch shell: the tap-zone map over the first page, until "Don't show this tip again".
  if (tenorchrome::kTouchShell && !preview && SETTINGS.readerTapTip) {
    readertip::open(ReaderUtils::tapRules(ReaderUtils::isRtlBookLanguage(epub->getLanguage()), true));
#ifdef TENOR_PRESS_PROBE
    LOG_INF("TIP", "open t=%lu", millis());
#endif
  }
  return true;
}

namespace {
void saveTapTipHidden() { SETTINGS.saveToFile(); }
}  // namespace

// The tap-zone map is up: it takes this pass's input. Its button hides it for good (one settings write,
// after the page is back on the panel); any other tap, swipe or Back closes it for this open only. The
// tap that closes it turns no page. The page comes back with 1 full refresh and its gray pass, nothing
// before them: a page turn's lighter waveform left the map's lines on the glass (founder 06/10).
bool EpubReaderActivity::handleTapTip() {
  int x = 0, y = 0;
  const bool tapped = mappedInput.wasScreenTapped(x, y);
  const bool closing = tapped || mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None ||
                       mappedInput.wasReleased(MappedInputManager::Button::Back) ||
                       mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  if (!closing) return true;
  if (tapped && readertap::tipButton(renderer.getScreenWidth(), renderer.getScreenHeight(), readertip::rules())
                    .contains(x, y)) {
    SETTINGS.readerTapTip = 0;
    activityManager.deferWrite(&saveTapTipHidden);
  }
#ifdef TENOR_PRESS_PROBE
  LOG_INF("TIP", "close tap=%d at=%d,%d off=%u t=%lu", tapped, x, y, SETTINGS.readerTapTip ? 0u : 1u, millis());
#endif
  RenderLock lock;
  readertip::close();
  pagesUntilFullRefresh = 1;
  requestUpdate();
  return true;
}

// Runs from onExit(). While reading, the thumbnail waited on idle passes for 96 KB free, which
// the page-turner radio never leaves (61 KB on the X3), so a new book reached Home without its
// cover; and with the radio off each decode held the buttons for about 3 s under the page. By
// the time the reader closes the ActivityManager has already released the radio and the page
// no longer matters, so both heights are written here in one pass over the cover. A sleep
// transition skips them: the power key must not wait on a decode; the next open records the miss
// again, and Home writes a thumbnail its card still lacks on an idle pass (writeMissingThumb).
void EpubReaderActivity::writePendingThumbs() {
  if (pendingThumbCount == 0 || !epub || activityManager.isSleepTransition()) return;
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("ERS", "EPUB_THUMB stage=before free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  // The decode holds the page still for 1 to 3 s on the X3; say so on the panel first.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  // The cover's copy out of the book inflates through a 32 KB window in one block, which a
  // reading session can leave the heap without (X3 r19: 32.756 B largest, both thumbnails lost).
  // The framebuffer holds nothing Home keeps, so it is lent for the pass.
  GfxRenderer::FrameBufferLoan loan(renderer);
  epub->generateThumbBmps(pendingThumbHeights, pendingThumbCount);
  pendingThumbCount = 0;
}

ChapterPosition EpubReaderActivity::chapterPosition() const {
  if (section) return {section->currentPage, section->estimatedTotalPages()};
  return {nextPageNumber, cachedChapterTotalPageCount};
}

int EpubReaderActivity::bookPercentFor(const ChapterPosition& position) const {
  if (epub && !epub->indexComplete()) return -1;  // unknown until the chapter sizes are built
  if (!epub || epub->getBookSize() == 0 || !position.hasTotal()) return 0;
  // The page index can run past the chapter's estimated total while it is still
  // building, so the fraction is clamped before the cast.
  const float fraction = epub->calculateProgress(currentSpineIndex, position.chapterFraction());
  return static_cast<int>(std::clamp(fraction, 0.0f, 1.0f) * 100.0f + 0.5f);
}

void EpubReaderActivity::openReaderMenu() {
  pendingManualTurn = 0;
  // The zone icon of a screen opened from the menu leads to the page, not back to the menu.
  if (activityManager.returningToReaderPage()) {
    requestUpdate();
    return;
  }
#ifdef TENOR_TURN_TRACE
  dropTurnTrace(pendingManualTurnTrace, "reader_menu");
  LOG_INF("ERS", "MENU_REQUEST t=%lu", millis());
#endif
  if (usesToolbarMenu()) {
    // Reached from a child activity's result handler (footnotes, bookmarks,
    // go-to-percent... cancelled back to the menu), so the framebuffer holds
    // that screen, not the page: re-render the page and let renderBook() put
    // the toolbar on top. The in-reader fast path is openOverlay().
    overlay = Overlay::Toolbar;
    focusedTool = 0;
    panelHoldJumped = false;
    panelCursorShown = !mappedInput.hasTouch();
    if (!toolbarUi) toolbarUi = std::make_unique<ReaderToolbarUi>(renderer);
    toolbarUi->begin();
    discardOverlayPage();
    requestUpdate();
    return;
  }

  // Child screens (chapter list, text settings) release the section to free its
  // pagination buffers; chapterPosition() covers that with the cached position.
  const ChapterPosition position = chapterPosition();
  const int bookProgressPercent = bookPercentFor(position);
  const uint8_t readerStatusBarHeightBeforeMenu = readerStatusBarHeight();

  pauseKeepsStatsInRam = true;
  startActivityForResult(
      std::make_unique<EpubReaderMenuActivity>(renderer, mappedInput, epub->getTitle(), position.displayPage(),
                                               position.totalPages, bookProgressPercent, SETTINGS.orientation,
                                               !currentPageFootnotes.empty(), !cachedBookmarks.empty()),
      [this, readerStatusBarHeightBeforeMenu](const ActivityResult& result) {
        const auto& menu = std::get<MenuResult>(result.data);

        if (SETTINGS.orientation != menu.orientation) {
          applyOrientation(menu.orientation);
        }

        toggleAutoPageTurn(menu.pageTurnOption);

        // The status-bar picker updates SETTINGS in place, then the menu closes
        // through the cancelled result path. Rebuild only when the reserved
        // reader height changes. Same-height modes need a footer repaint only.
        if (result.isCancelled && readerStatusBarHeight() != readerStatusBarHeightBeforeMenu) {
          RenderLock lock;
          danLaiTrang();
        }

        if (!result.isCancelled) {
          onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action), menu);
        }
      });
}

bool EpubReaderActivity::deferBackgroundBuildForBle() const {
#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
  return bleturner::holdsHeap();
#else
  return false;
#endif
}

bool EpubReaderActivity::backgroundBuildStartHeapGate() {
  // Admitting a parked build loads the parser again, which drops the free heap by whatever the
  // last park handed back. Require room for that on top of the tick budget the resumed build
  // has to stay above, or the resume buys one page and pays for the next park straight after.
  const size_t admission = parkedParserFootprint > 0
                               ? std::max<size_t>(BACKGROUND_BUILD_START_MIN_FREE_HEAP,
                                                  parkedParserFootprint + BACKGROUND_BUILD_MIN_FREE_HEAP)
                               : BACKGROUND_BUILD_START_MIN_FREE_HEAP;
  return !backgroundBuildFailed && !deferBackgroundBuildForBle() && ESP.getFreeHeap() >= admission &&
         ESP.getMaxAllocHeap() >= BACKGROUND_BUILD_START_MIN_MAX_ALLOC;
}

bool EpubReaderActivity::buildTickHeapGate() {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t maxBlock = ESP.getMaxAllocHeap();
  buildHeapPaused = freeHeap < BACKGROUND_BUILD_MIN_FREE_HEAP || maxBlock < BACKGROUND_BUILD_MIN_MAX_ALLOC;
  return !buildHeapPaused;
}

bool EpubReaderActivity::backgroundBuildCanTick() {
  if (!section || !section->isBuilding() || backgroundBuildFailed || deferBackgroundBuildForBle())
    return false;
  // The loop clears a one-pass park latch before it reaches this predicate.
  // Keeping the predicate side-effect free also prevents skipLoopDelay() from
  // admitting a just-parked parser in the same frame.
  if (backgroundBuildSuspended) return false;
  if (section->isBuildParked()) return backgroundBuildStartHeapGate();
  return buildTickHeapGate();
}

bool EpubReaderActivity::waitsForIndex() {
  if (!epub || epub->indexComplete()) return false;
  showIndexingMessage = true;
  indexingMessageTime = millis();
  requestUpdate();
  return true;
}

bool EpubReaderActivity::holdsRadio() const {
  return !preview && epub && !epub->indexComplete() && indexFailures < INDEX_MAX_FAILURES;
}

bool EpubReaderActivity::yieldForRadio() {
  if (!section || !section->isBuilding() || section->isBuildParked()) return true;
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) return false;
  // X3 r43: the radio came up beside the live layout parser and left a largest block of 32,756 B,
  // under the 32,768 its own check keeps, so the start was rolled back and the parser parked 640 ms
  // later anyway (the radio defers the build). Parked first, its pages stay and it resumes at its
  // checkpoint.
  backgroundBuildSuspended = true;
  suspendBackgroundBuild();
  return true;
}

// Turning into a chapter not laid out yet started its build inside the paint: 724 ms of the 1.6 s
// the X3 took to a readable page (r39). On the last page of a whole chapter a quiet pass lays out
// the next one's first pages into a partial section file, which the turn loads like any cached
// page and the look-ahead extends. Only on the heap a build needs to start, so never beside the
// radio, and only for a chapter small enough to unpack in a pass.
bool EpubReaderActivity::nextChapterDue(const bool inputThisPass) {
  if (inputThisPass || preview || !epub || !epub->indexComplete() || !section || section->isBuilding() ||
      section->isPartial() || section->pageCount == 0 ||
      section->currentPage < static_cast<int>(section->pageCount) - 1 || buildViewportWidth == 0 ||
      lastRenderCompleteMs == 0 || millis() - lastRenderCompleteMs < NEXT_CHAPTER_QUIET_MS)
    return false;
  const int spine = currentSpineIndex + 1;
  if (spine >= epub->getSpineItemsCount() || nextChapterPrepared == spine) return false;
  const size_t bytes = epub->getCumulativeSpineItemSize(spine) - epub->getCumulativeSpineItemSize(spine - 1);
  return bytes <= BUILD_POPUP_BYTE_THRESHOLD && backgroundBuildStartHeapGate();
}

void EpubReaderActivity::prepareNextChapter() {
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) return;
  const int spine = currentSpineIndex + 1;
  nextChapterPrepared = spine;
  // At the clock the loop drops to after 3 s without a key the layout runs 12 times slower.
  HalPowerManager::Lock fullSpeed;
  const unsigned long started = millis();
  const ReaderRenderSpec spec = SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight);
  Section next(epub, spine, renderer, preview);
  if (next.loadSectionFile(spec)) return;  // laid out already, whole or in part
  // A key stops it between layout steps: the turn it asks for goes first, and lays out what is missing.
  const auto keyDown = [] { return gpio.rawInputActive(); };
  if (keyDown() || !next.startBuild(spec)) return;
  while (!next.isBuildComplete() && static_cast<int>(next.pageCount) <= LOOK_AHEAD_PAGES && !keyDown() &&
         millis() - started < static_cast<unsigned long>(BUILD_WINDOW_MAX_MS) &&
         next.buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK)) {
  }
  // Only whole pages go to the card; a chapter laid out to its end is already there.
  next.suspendBuild();
  LOG_PROBE("ERS", "NEXT_CHAPTER spine=%d pages=%u ms=%lu", spine, static_cast<unsigned>(next.pageCount),
          millis() - started);
}

bool EpubReaderActivity::indexStepDue() const {
  return holdsRadio() && section && overlay == Overlay::None &&
         !automaticPageTurnActive && !pendingPercentJump && pendingAnchor.empty() && pendingQuoteEdit.empty() &&
         lastRenderCompleteMs != 0 && millis() - lastRenderCompleteMs > INDEX_QUIET_MS &&
         (indexRetryAtMs == 0 || static_cast<long>(millis() - indexRetryAtMs) >= 0);
}

void EpubReaderActivity::runIndexStep() {
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) return;
  // A chapter's layout keeps its parser resident once the pages ahead are laid out, holding the
  // heap a step needs, for as long as the chapter lasts (X3 r19: no step ran in 30 s). Park it the
  // way the radio does; its pages stay, and it resumes when the reader nears the last of them.
  if (section->isBuilding() && !section->isBuildParked()) {
    backgroundBuildSuspended = true;
    suspendBackgroundBuild();
  }
  // The loop drops the CPU to its low-power clock after 3 s without a key, which is when steps
  // run: at that clock the TOC step took 57 s on the X3 (r21) against 5 s in the foreground.
  HalPowerManager::Lock fullSpeed;
  // Straight from the key hardware: this pass is held until the step returns.
  const Epub::IndexStep step = epub->indexSome([] { return gpio.rawInputActive(); });
  if (step == Epub::IndexStep::Done) {
    tocSpineCached = -1;  // the chapter's TOC range can be read now
    LOG_PROBE("ERS", "Book index complete");
    dropSectionsLaidOutWithoutToc();
  } else if (step == Epub::IndexStep::Failed) {
    indexRetryAtMs = millis() + INDEX_RETRY_MS;
    if (++indexFailures == INDEX_MAX_FAILURES) LOG_ERR("ERS", "Book index given up");
  }
  if (step != Epub::IndexStep::Stopped) {
    indexStops = 0;
  } else if (++indexStops == INDEX_MAX_STOPS) {
    indexFailures = INDEX_MAX_FAILURES;
    LOG_ERR("ERS", "Book index given up");
  }
}

// Every section on the card was laid out while the book had no TOC (book.part starts with an empty
// sections folder), so none of them breaks pages at, or can find, its chapter's TOC anchors: a TOC
// jump to a chapter inside one of them went nowhere. The ones whose chapter has such anchors go,
// and the chapter on screen is laid out again from the words the reader is at.
void EpubReaderActivity::dropSectionsLaidOutWithoutToc() {
  std::vector<int> spines;
  {
    auto dir = Storage.open((epub->getCachePath() + "/sections").c_str());
    if (!dir) return;
    char name[32];
    // The chapters laid out while the index was building: a handful, bounded all the same.
    constexpr size_t MAX_SECTIONS = 256;
    for (auto entry = dir.openNextFile(); entry && spines.size() < MAX_SECTIONS; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));
      entry.close();
      char* end = nullptr;
      const long spine = std::strtol(name, &end, 10);
      if (end != name && std::strcmp(end, ".bin") == 0) spines.push_back(static_cast<int>(spine));
    }
  }
  // The chapter on screen may still be laying out (its pages in a staging file, no .bin yet).
  if (section && std::find(spines.begin(), spines.end(), currentSpineIndex) == spines.end()) {
    spines.push_back(currentSpineIndex);
  }
  for (const int spine : spines) {
    const int first = epub->getTocIndexForSpineIndex(spine);
    bool anchored = false;
    for (int i = std::max(first, 0); first >= 0 && i < epub->getTocItemsCount() && !anchored; ++i) {
      const auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spine) break;
      anchored = !entry.anchor.empty();
    }
    if (!anchored) continue;
    if (section && spine == currentSpineIndex) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
      section.reset();
    }
    Section(epub, spine, renderer).clearCache();
  }
}

bool EpubReaderActivity::releaseHeapForBuild() {
  // The rebuildable font caches go first, once a paint. X3 with a remote linked, 02/10: six
  // builds starved at about 16,180 B free against a 16,384 B floor, each one stopping the radio
  // (~51 KB) and dropping the remote until the page was shown.
  if (!fontsShedForBuild) {
    fontsShedForBuild = true;
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->releaseSdFontCaches();
      LOG_INF("ERS", "Font caches shed for a starved build free=%u largest=%u",
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      return true;
    }
  }
#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
  if (radioReleasedForBuild) return false;
  const auto released = bleturner::beforeChapterBuild();
  if (released == bleturner::BuildRelease::NotHeld) return false;
  // A radio still up keeps its heap: another try at the build would starve again. The latch
  // stays, so the caller goes to the memory notice once instead of waiting out a second timeout.
  radioReleasedForBuild = true;
  heapMapDump("radio-stopped-for-build");
  return released == bleturner::BuildRelease::Released;
#else
  return false;
#endif
}

// A jump whose target the heap could not lay out. The section stays, so nothing clears the
// target the way loading a section does: left pending, it would land on the next chapter loaded
// (a percent of it, an anchor, a page) and that wrong page would be saved as the progress.
void EpubReaderActivity::forgetPendingJump() {
  pendingPercentJump = false;
  pendingAnchor.clear();
  pendingOffsetJump.reset();
  pendingPageJump.reset();
}

// A jump or an open whose chapter the heap could not lay out. The section was made for that chapter
// in this paint, so its page is 0: painted and saved, the reader would lose their place (review
// V-A). With a saved place (a jump) the reader goes back there and paints it; with none yet (an
// open) it keeps its target, pending offset included, and the memory notice stays until the next
// input tries again. No page of the target chapter is painted or saved.
void EpubReaderActivity::stayAfterStarvedJump() {
  const bool backToSaved = lastSavedSpineIndex >= 0 && lastSavedSpineIndex != currentSpineIndex;
  if (lastSavedSpineIndex >= 0) {
    forgetPendingJump();
    currentSpineIndex = lastSavedSpineIndex;
    nextPageNumber = lastSavedPage;
  }
  section.reset();
  // Only a move back to another chapter repaints: the saved chapter starving too stays put here
  // instead of asking for the same paint again.
  if (backToSaved) requestUpdate();
}

// Heap ran out while extending the section and nothing else could be freed. Keep the
// pages already built and the reading position; the next input shows the last built page.
void EpubReaderActivity::showMemoryError() {
#ifdef TENOR_TURN_TRACE
  tracePaint("ERROR", "memory");
  logTurnTrace("FAILED", appliedTurnTrace, "memory");
  appliedTurnTrace = {};
#endif
  LOG_ERR("ERS", "Section build starved of heap with nothing left to release free=%u largest=%u",
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  section->suspendBuild();
  if (section->pageCount > 0 && section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
  }
  settleBuildPopup();
  renderer.clearScreen();
  GUI.drawPopup(renderer, tr(STR_MEMORY_ERROR));
  automaticPageTurnActive = false;
  // The page shown after this is not the one a waiting reselection asked for.
  pendingQuoteEdit.clear();
}

void EpubReaderActivity::suspendBackgroundBuild() {
  if (!section || !section->isBuilding() || section->isBuildParked()) return;
  const bool heapPressure = !buildTickHeapGate();
  if (!heapPressure && !backgroundBuildSuspended && !deferBackgroundBuildForBle() && !backgroundBuildFailed) return;
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("ERS_TRACE", "BUILD_SUSPEND t=%lu spine=%d page=%d count=%u partial=%u heap=%u largest=%u ble=%u pressure=%u",
          millis(), currentSpineIndex, section->currentPage, static_cast<unsigned>(section->pageCount),
          section->isPartial() ? 1u : 0u, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), deferBackgroundBuildForBle() ? 1u : 0u,
          heapPressure ? 1u : 0u);
#endif
  // The small checkpoint releases parser/CSS memory while completed pages stay
  // in the active staging file. Unsupported boundaries keep the partial-commit fallback.
  const size_t freeBeforePark = ESP.getFreeHeap();
  const bool parked = !backgroundBuildFailed && section->parkBuild();
  if (!parked) section->suspendBuild();
  if (parked) {
    // What the park just released is what the next resume will take back.
    const size_t freeAfterPark = ESP.getFreeHeap();
    if (freeAfterPark > freeBeforePark) parkedParserFootprint = freeAfterPark - freeBeforePark;
  }
  // BLE policy is reversible when the radio becomes idle. A parked
  // heap-pressure build can also resume after the released memory recovers;
  // keep a one-pass latch for every park so the same loop cannot immediately
  // re-admit the parser. A partial-commit fallback stays latched as well.
  backgroundBuildSuspended = parked || heapPressure;
  LOG_INF("ERS", "Build parser released for render headroom: parked=%u page=%d pages=%u free=%u largest=%u",
          parked ? 1u : 0u, section->currentPage, static_cast<unsigned>(section->pageCount),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

void EpubReaderActivity::showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (!buildPopupPending || !renderer.hasFrameBuffer()) return;
#ifdef TENOR_PRESS_PROBE
  // A build that ran past the deadline, over a watermark (partial=1) or while opening a large file.
  LOG_INF("ERS", "BUILD_POPUP src=deadline page=%d pages=%u partial=%u", section ? section->currentPage : -1,
          section ? static_cast<unsigned>(section->pageCount) : 0u, section && section->isPartial() ? 1u : 0u);
#endif
  // The layout goes on while the panel shows the popup: waiting it out held a contents jump 390 ms
  // (X3 r43). Nothing is drawn before settleBuildPopup().
  GUI.drawPopup(renderer, tr(STR_INDEXING), false);
  renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
  buildPopupRefreshing = true;
  pagesUntilFullRefresh = 1;
  buildPopupPending = false;
}

void EpubReaderActivity::settleBuildPopup() {
  if (!buildPopupRefreshing) return;
  renderer.waitRefreshComplete();
  buildPopupRefreshing = false;
}

void EpubReaderActivity::openDictionaryWordSelect(const bool quotation, const std::string& editName) {
  if (!quotation && SETTINGS.dictionaryName[0] == '\0') {
    showDictionaryMessage = true;
    dictionaryMessageTime = millis();
    requestUpdate();
    return;
  }
  if (!section) return;
  QuoteRecord existing;
  if (!editName.empty() && !quotes::load(editName, existing)) return;
  auto page = section->loadPage(section->currentPage);
  if (!page) return;

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  readingMargins(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  auto selector = makeUniqueNoThrow<DictionaryWordSelectActivity>(renderer, mappedInput, std::move(page),
                                                                  orientedMarginLeft, orientedMarginTop);
  if (!selector) return;
  if (!editName.empty())
    selector->editQuotation(std::move(existing), editName, currentSpineIndex, section->currentPage);
  else if (quotation)
    selector->selectQuotation({bookPath, getBookTitle(), "", currentSpineIndex, section->currentPage, 0});
  startActivityForResult(std::move(selector), [this, quotation](const ActivityResult&) {
    // A quote saved on that screen has to be drawn on the page we return to. The result
    // handler runs with the render lock released, so take it here: drawQuoteHighlights
    // walks quoteAnchors on the render task, and a rebuild frees the buffer it is walking.
    if (quotation) {
      RenderLock lock;
      quotes::loadAnchors(bookPath, quoteAnchors);
    }
    requestUpdate();
  });
}

void EpubReaderActivity::openBookQuotes() {
  // The screen is handed the book already open here, so the detail names chapters from its
  // table of contents instead of opening the book a second time.
  auto screen = makeUniqueNoThrow<QuotesActivity>(renderer, mappedInput, bookPath, /*insideReader=*/true, epub);
  if (!screen) return;
  startActivityForResult(
      std::move(screen),
      [this](const ActivityResult& result) {
        // Quotes may have been deleted, trimmed or rewritten on that screen, so the anchors
        // are read again whatever it returned. Same lock the save path takes:
        // drawQuoteHighlights walks quoteAnchors on the render task.
        {
          RenderLock lock;
          quotes::loadAnchors(bookPath, quoteAnchors);
        }
        if (const auto* edit = std::get_if<QuoteEditResult>(&result.data); edit && !result.isCancelled) {
          jumpToQuoteForEdit(edit->name);
        }
        requestUpdate();
      });
}

void EpubReaderActivity::jumpToQuoteForEdit(const std::string& name) {
  QuoteRecord quote;
  if (!quotes::load(name, quote) || quote.path != bookPath) return;
  RenderLock lock;
  pendingQuoteEdit = name;
  quoteEditPageShown = false;
  // A spine item this book does not have (the file was replaced by another edition) leaves
  // the reader where it is; the selector then says the quote is not on this page.
  if (quote.spine < 0 || quote.spine >= epub->getSpineItemsCount()) return;
  clearDeferredReposition();
  const int page = std::max(0, quote.page);
  // The two roads the bookmark jump takes: an anchored quote is found by its visible text
  // offset, which survives a change of font size; an older one only knows its page number.
  if (section && currentSpineIndex == quote.spine) {
    section->currentPage =
        quote.hasAnchor ? section->getPageForVisibleTextOffset(quote.anchorStart).value_or(page) : page;
  } else {
    currentSpineIndex = quote.spine;
    TRACE_JUMP_BEGIN("quote");
    if (quote.hasAnchor) pendingOffsetJump = quote.anchorStart;
    nextPageNumber = page;
    section.reset();
  }
}

bool EpubReaderActivity::externalPageTurnAllowed() const {
  return !preview && overlay == Overlay::None;
}

bool EpubReaderActivity::requestShortcut(const ReaderShortcut shortcut) {
  // An open toolbar already owns the page; a preview owns its keys.
  if (!externalPageTurnAllowed()) return false;
  pendingShortcut = shortcut;
  return true;
}

bool EpubReaderActivity::manualPageTurnReady() const {
  return millis() - lastPageTurnTime >= 200;
}

// latTrangThat lets a forward turn step past the pages laid out so far while the chapter is
// still being laid out; the paint then lays out up to that page, or turns on into the next
// chapter if the chapter ends first. A chapter laid out to its end has nothing left to wait
// for, even one with no pages.
bool EpubReaderActivity::pageAwaitsLayout() const {
  return section && (section->isBuilding() || section->isPartial()) &&
         section->currentPage >= static_cast<int>(section->pageCount);
}

void EpubReaderActivity::openFootnoteSelect(const bool reopenMenuOnCancel) {
  if (!section || currentPageFootnotes.empty()) return;
  if (currentPageFootnotes.size() == 1) {
    navigateToHref(currentPageFootnotes[0].href, true);
    return;
  }

  auto page = section->loadPage(section->currentPage);
  if (!page) return;

  // The page is drawn again under the markers, so it takes the margins the reader laid it out in.
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  readingMargins(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  auto selector = makeUniqueNoThrow<EpubReaderFootnoteSelectActivity>(renderer, mappedInput, std::move(page),
                                                                      orientedMarginLeft, orientedMarginTop);
  if (!selector) {
    LOG_ERR("ERS", "OOM: EpubReaderFootnoteSelectActivity");
    return;
  }
  startActivityForResult(std::move(selector), [this, reopenMenuOnCancel](const ActivityResult& result) {
    if (result.isCancelled) {
      if (reopenMenuOnCancel) {
        openReaderMenu();
      } else {
        requestUpdate();
      }
      return;
    }
    const auto& footnoteResult = std::get<FootnoteResult>(result.data);
    navigateToHref(footnoteResult.href, true);
    requestUpdate();
  });
}

void EpubReaderActivity::loop() {
  // Taken once: a pass that returns before it reaches its branch below drops it, so it
  // never fires later over a menu or overlay that owned this pass.
  const ReaderShortcut shortcut = std::exchange(pendingShortcut, ReaderShortcut::None);
  stayAfterDroppedExit();
  if (!epub) {
    finish();
    return;
  }

  // The open is committed, so its first frame is up. Thumbnails still owed are written as the
  // reader closes, but not on the power key; Home then writes them, and without this file it loads
  // the whole book to find the cover (1,3 s for 5.000 chapters on the X3).
  if (coverRefPending && !openCommitPending) {
    coverRefPending = false;
    std::string saved;
    if (pendingThumbCount > 0 && !(coverref::load(epub->getCachePath(), saved) && saved == epub->getCoverHref())) {
      const bool ok = coverref::save(epub->getCachePath(), epub->getCoverHref());
      LOG_DBG("ERS", "Cover ref saved ok=%u", ok ? 1u : 0u);
    }
  }

  // The first frame is up: the stack lives in RAM now, so the file read at the open goes. An
  // unclean shutdown then cannot bring a stale stack back; the exit writes it again.
  if (linkStackOnCard && !openCommitPending) {
    linkStackOnCard = false;
#ifdef TENOR_PRESS_PROBE
    const unsigned long dropStarted = millis();
#endif
    Storage.remove((epub->getCachePath() + "/links.bin").c_str());
#ifdef TENOR_PRESS_PROBE
    LOG_INF("ERS", "LINKS_DROP ms=%lu", millis() - dropStarted);
#endif
  }

  // Someone else turned the screen while this reader was stacked (the control
  // center's orientation tile). Reflow before the next render, or the page
  // would be drawn with a layout built for the previous frame size.
  if (appliedOrientation != SETTINGS.orientation) {
    applyOrientation(SETTINGS.orientation);
    requestUpdate();
    return;
  }

#if defined(FREEINK_CAP_BLE_HID_HOST) && FREEINK_CAP_BLE_HID_HOST
  // The radio was stopped to finish a starved section build. Once the render task
  // has released the lock the page is on screen, so ask the main loop to restart it.
  if (radioReleasedForBuild) {
    RenderLock lock(RenderLock::TryTake{});
    if (lock.acquired()) {
      radioReleasedForBuild = false;
      bleturner::afterPaint();
    }
  }
#endif

  // A quote handed back for reselection: its page is on the panel now, so the selector opens
  // over it. The flag is consumed under the lock so a render still in flight finishes first.
  if (quoteEditPageShown) {
    std::string name;
    {
      RenderLock lock(RenderLock::TryTake{});
      if (lock.acquired()) {
        quoteEditPageShown = false;
        name.swap(pendingQuoteEdit);
      }
    }
    if (!name.empty()) {
      openDictionaryWordSelect(true, name);
      return;
    }
  }
  // A button pressed before the quote's page reached the panel moves the reader on its own
  // way, so the reselection is dropped; left waiting it would open over a later page.
  if (!pendingQuoteEdit.empty() && !quoteEditPageShown && mappedInput.wasAnyPressed()) {
    RenderLock lock;
    if (!quoteEditPageShown) pendingQuoteEdit.clear();
  }

  constexpr unsigned long IDLE_PREWARM_DEBOUNCE_MS = 400;
  // Idle work below blocks this pass for 50 ms to seconds on the X3. A pass that carries a
  // button edge, or has turns queued behind the paint, handles them first; the idle work
  // waits for a quiet pass.
  const bool inputThisPass =
      mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased() || pendingManualTurn != 0 || pendingExternalTurn != 0;
  if (!inputThisPass && textCloseFrame.load(std::memory_order_acquire) == 2) {
    RenderLock lock(RenderLock::TryTake{});
    if (lock.acquired() && textCloseFrame.load(std::memory_order_acquire) == 2) flushTextSettingsLocked();
  }
  // USB power came or went while reading: the battery follows it now instead of on the next turn.
  if (gpio.wasUsbStateChanged()) statusBarStale = true;
  if (statusBarStale && !inputThisPass && readingPageVisible() && !paintDropped) {
    RenderLock lock(RenderLock::TryTake{});
    if (lock.acquired()) {
      statusBarStale = false;
      repaintStatusBarAlone();
    }
  }
  // The section and the framebuffer belong to the render task while it paints, and it can replace
  // or free the section: every check that reads them runs under a lock taken without waiting, and
  // a pass that finds the render task painting skips the work instead of stalling behind it.
  if (!inputThisPass && lastRenderCompleteMs != 0 && millis() - lastRenderCompleteMs > IDLE_PREWARM_DEBOUNCE_MS &&
      ESP.getFreeHeap() > RENDER_MIN_FREE_HEAP && ESP.getMaxAllocHeap() > BACKGROUND_BUILD_MIN_MAX_ALLOC) {
    RenderLock lock(RenderLock::TryTake{});
    if (lock.acquired() && section && (!section->isBuilding() || section->isBuildParked()) &&
        renderer.hasFrameBuffer() &&
        (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
      idlePrewarmSpine = currentSpineIndex;
      idlePrewarmPage = section->currentPage;
      const int nextPage = section->currentPage + 1;
      if (nextPage < static_cast<int>(section->pageCount)) {
#ifdef TENOR_UI_ACCEPTANCE
        LOG_DBG("ERS_TRACE", "PREWARM_LOAD_BEGIN t=%lu spine=%d page=%d heap=%u largest=%u", millis(),
                currentSpineIndex, nextPage, static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
        if (const auto p = section->loadPage(nextPage)) {
#ifdef TENOR_UI_ACCEPTANCE
          LOG_DBG("ERS_TRACE", "PREWARM_LOAD_END t=%lu spine=%d page=%d ok=1 offset=%u heap=%u largest=%u",
                  millis(), currentSpineIndex, nextPage, static_cast<unsigned>(p->visibleTextOffset),
                  static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
          if (auto* fcm = renderer.getFontCacheManager()) {
            const auto t0 = millis();
            auto scope = fcm->createPrewarmScope();
            p->render(renderer, SETTINGS.getReaderFontId(), 0, 0);
            scope.endScanAndPrewarm();
            LOG_DBG("ERS", "Idle prewarm: page %d in %lums", nextPage, millis() - t0);
          }
        }
      }
    }
  }

  // Background section builds release their parser for BLE or when the tick budget fails.
  // A newly parked builder waits until the next loop pass before resuming. This
  // prevents a park followed by an immediate tick in the same pass from
  // burning the memory headroom that the park just created.
  bool backgroundBuildParkedThisLoop = false;
  // A parked parser resumes on the next loop pass once the radio is idle and
  // the released heap is available. The latch stays set through the remainder
  // of the pass that performed the park, including skipLoopDelay().
  if (backgroundBuildSuspended) {
    RenderLock lock(RenderLock::TryTake{});
    if (lock.acquired() && section && section->isBuildParked() && !deferBackgroundBuildForBle() &&
        backgroundBuildStartHeapGate()) {
      backgroundBuildSuspended = false;
    }
  }
  {
    RenderLock lock(RenderLock::TryTake{});
    if (lock.acquired() && section && section->isBuilding() && !section->isBuildParked() &&
        (backgroundBuildSuspended || backgroundBuildFailed || deferBackgroundBuildForBle() || !buildTickHeapGate())) {
      suspendBackgroundBuild();
      backgroundBuildParkedThisLoop = section && section->isBuildParked();
    }
  }

  if (buildViewportWidth > 0 && !partialRebuildStartFailed && !backgroundBuildSuspended) {
    RenderLock lock(RenderLock::TryTake{});
    if (lock.acquired() && section && !section->isBuilding() && section->isPartial() &&
        section->currentPage + PARTIAL_REBUILD_START_MARGIN >= static_cast<int>(section->pageCount) &&
        backgroundBuildStartHeapGate()) {
      const ReaderRenderSpec buildSpec = SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight);
      if (!section->startBuild(buildSpec)) {
        partialRebuildStartFailed = true;
        LOG_ERR("ERS", "Failed to start deferred partial extension build");
      } else {
        LOG_DBG("ERS", "Reader near partial watermark (%d/%d), resuming extension build", section->currentPage,
                section->pageCount);
        suspendBackgroundBuild();
      }
    }
  }

  // With the radio up the parser stays parked (deferBackgroundBuildForBle) and the reader
  // sits on the last laid-out page, so every turn resumed the parser inside the paint:
  // 140 to 400 ms on the X3 before the page could even load. Once the page has been on
  // the panel for a beat, lay out the next pages here instead, once per page. It takes
  // the same heap the paint's own resume takes, never while the radio is still
  // allocating its start, and the park below hands the parser straight back.
  // A book reopened on the last page of its partial cache has no parser to resume: the radio
  // starts before the extension can, so the extension starts here, as the first turn's paint
  // would start it (780 ms inside that paint on the X3).
  if (bleturner::status().starting) radioSettledMs = millis();
  const unsigned long sincePaint = std::min(millis() - lastRenderCompleteMs, millis() - radioSettledMs);
  // Conditions that do not read the section; the rest is read under the lock below.
  const bool lookAheadWindow = !inputThisPass && !backgroundBuildFailed && !backgroundBuildParkedThisLoop &&
                               deferBackgroundBuildForBle() && !bleturner::status().starting &&
                               lastRenderCompleteMs != 0 && sincePaint > BUILD_WINDOW_QUIET_MS &&
                               sincePaint < BUILD_WINDOW_LATEST_MS;
  if (lookAheadWindow || (!inputThisPass && !backgroundBuildParkedThisLoop)) {
    RenderLock lock(RenderLock::TryTake{});
    const bool lookAhead =
        lock.acquired() && lookAheadWindow && section &&
        (section->isBuildParked() ||
         (section->isPartial() && !section->isBuilding() && !partialRebuildStartFailed && buildViewportWidth > 0)) &&
        lookAheadPage != section->currentPage &&
        section->currentPage + LOOK_AHEAD_PAGES >= static_cast<int>(section->pageCount);
    if (lock.acquired() &&
        (lookAhead || (!inputThisPass && !backgroundBuildParkedThisLoop && backgroundBuildWanted() &&
                       backgroundBuildCanTick()))) {
      if (lookAhead) lookAheadPage = section->currentPage;
      const unsigned long tickStarted = millis();
#ifdef TENOR_PRESS_PROBE
      if (lookAhead) buildprobe::reset();
#endif
#ifdef TENOR_TURN_TRACE
      const unsigned startedExtension = lookAhead && !section->isBuilding() ? 1u : 0u;
#endif
#ifdef TENOR_UI_ACCEPTANCE
      traceBuildTickBegin("background");
#endif
      if (!section->isBuilding() &&
          !section->startBuild(SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight))) {
        partialRebuildStartFailed = true;
        LOG_ERR("ERS", "Failed to start look-ahead extension build");
      } else if (![&] {
                   // One tick is about 20 ms of parsing and often adds no page, so the look-ahead
                   // keeps ticking until the pages after this one are laid out; handing the parser
                   // back after one tick left the first turn to lay it out in its paint (r27-ui).
                   bool ticked = section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK);
                   while (lookAhead && ticked && !section->isBuildComplete() &&
                          static_cast<int>(section->pageCount) <= section->currentPage + LOOK_AHEAD_PAGES &&
                          millis() - tickStarted < static_cast<unsigned long>(BUILD_WINDOW_MAX_MS))
                     ticked = section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK);
                   return ticked;
                 }()) {
        if (section->buildStarved()) {
          // The build is parked with its pages intact. The next foreground demand
          // resumes it, freeing the radio first if it has to.
          buildHeapPaused = true;
        } else {
          LOG_ERR("ERS", "Background section build failed");
          backgroundBuildFailed = true;
          nextPageNumber = section->currentPage;
          section.reset();
          requestUpdate();
        }
      } else if (section->isBuildComplete() && applyDeferredReposition()) {
        requestUpdate();
      }
#ifdef TENOR_UI_ACCEPTANCE
      if (section) {
        traceBuildTickEnd("background");
      }
#endif
      // A tick can cross the budget while laying out a paragraph. Release before the next frame.
      suspendBackgroundBuild();
#ifdef TENOR_TURN_TRACE
      if (lookAhead)
        LOG_INF("ERS", "LOOK_AHEAD start=%u pages=%u ms=%lu", startedExtension,
                section ? static_cast<unsigned>(section->pageCount) : 0u, millis() - tickStarted);
#endif
#ifdef TENOR_PRESS_PROBE
      if (lookAhead) buildprobe::log("look_ahead");
#endif
    }
  }

  catchUpTick(inputThisPass);

  if (nextChapterDue(inputThisPass)) prepareNextChapter();

  if (!inputThisPass && indexStepDue()) runIndexStep();

  if (handlePreviewInput()) return;

  // A paint that had a turn waiting behind it left its progress write; the turn's own paint
  // normally writes it, and a quiet pass writes it when that turn never came (opposite presses).
  if (!inputThisPass && !progressSaveFailed && progressSaveDeferred.load(std::memory_order_acquire)) {
    RenderLock lock(RenderLock::TryTake{});
    if (lock.acquired()) saveProgressIfMoved();
  }

  const bool atEndOfBook = currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();
  clearEndOfBookOptionsIfNeeded();

  // Only once the open itself is on the list (commitOpen), or the pending add would bring back
  // an entry this removes.
  if (SETTINGS.removeReadBooksFromRecents && !openCommitPending) {
    if (atEndOfBook && !recentsEntryRemoved) {
      recentsEntryRemoved = RECENT_BOOKS.removeByPath(epub->getPath());
    } else if (!atEndOfBook && recentsEntryRemoved) {
      RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());
      recentsEntryRemoved = false;
    }
  }

  if (atEndOfBook) {
    pendingReadFolderMove = SETTINGS.moveFinishedToReadFolder && !isInReadFolder(epub->getPath());
  } else {
    pendingReadFolderMove = false;
  }

  if (tenorchrome::kTouchShell && readertip::isOpen() && handleTapTip()) return;

  const auto touch =
      ReaderUtils::detectTouchPageTurn(renderer, mappedInput, ReaderUtils::isRtlBookLanguage(epub->getLanguage()),
                                       tenorchrome::kTouchShell);

  if (showBookmarkMessage && (millis() - bookmarkMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showBookmarkMessage = false;
    requestUpdate();
  }

  if (showDictionaryMessage && (millis() - dictionaryMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showDictionaryMessage = false;
    requestUpdate();
  }

  if (showIndexingMessage && (millis() - indexingMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showIndexingMessage = false;
    requestUpdate();
  }

  // The toolbar reader menu owns all input while shown, ahead of the automatic page turn
  // below: the More panel's rate popup switches automatic turning on and leaves the panel
  // open, so the timer must neither flip the page under it nor eat the panel's next
  // Confirm/Back release.
  if (overlay != Overlay::None) {
    pendingExternalTurn = 0;
#ifdef TENOR_TURN_TRACE
    dropTurnTrace(pendingExternalTurnTrace, "overlay");
#endif
    if (usesToolbarMenu()) {
      // Hold the interval at zero elapsed so closing the panel starts a fresh one.
      lastPageTurnTime = millis();
      handleOverlayInput();
      return;
    }
    // The style was switched off while an overlay was up (Settings reached via
    // the More panel); fall back to the clean page.
    {
      RenderLock lock;
      overlay = Overlay::None;
      discardOverlayPage();
      panelClosedLocked(false, false);
    }
    requestUpdate();
    return;
  }

  switch (mappedInput.homeButtonAction()) {
    case HomeButtonAction::ReaderMenu:
    case HomeButtonAction::Bookmark:
    case HomeButtonAction::Sync:
    case HomeButtonAction::Dictionary:
    case HomeButtonAction::Footnotes:
      automaticPageTurnActive = false;
      break;
    default:
      break;
  }

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        ReaderUtils::isTouchMenuGesture(renderer, mappedInput, tenorchrome::kTouchShell) ||
        // Touch shell: the menu's way in, the foot band, stops the automatic turn as the centre did.
        (tenorchrome::kTouchShell &&
         ReaderUtils::tapZone(renderer, mappedInput, false, true) == readertap::Zone::TextMenu)) {
      automaticPageTurnActive = false;
      pendingExternalTurn = 0;
#ifdef TENOR_TURN_TRACE
      dropTurnTrace(pendingExternalTurnTrace, "auto_cancel");
#endif
      requestUpdate();
      return;
    }

    if (!section && !xemTruoc) {  // a preview page turns like a laid-out one (latTrangThat)
      requestUpdate();
      return;
    }

    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
#ifdef TENOR_TURN_TRACE
      currentTurnTrace = detectTurnTrace("automatic", true);
#endif
      if (pageTurn(true)) requestUpdate();
      return;
    }
  }

  // While the end-of-book suggestion menu is up it owns Confirm/Back/navigation, so it
  // gets this tick's input first and the long-press shortcuts below stay inert behind it
  // -- a hold there must not drop a bookmark onto the suggestion screen or paint the
  // dictionary word picker over it. Anything the menu does not handle (long-press Back to
  // the file browser, say) still falls through to the regular handlers.
  if (handleEndOfBookMenu()) {
    pendingExternalTurn = 0;
#ifdef TENOR_TURN_TRACE
    dropTurnTrace(pendingExternalTurnTrace, "end_menu");
#endif
    return;
  }
  const bool endOfBookMenuOpen = endOfBookMenuActive();

  const unsigned long confirmHoldMs = confirmLongPressThreshold();
  // wasLongPressed() suppresses the release that follows it, so leave it unpolled while
  // the end-of-book menu owns Confirm -- otherwise the menu never sees that release.
  const bool confirmLongPressed = !endOfBookMenuOpen && confirmHoldMs != 0 &&
                                  mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, confirmHoldMs);
  bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  // A quick action's shortcut takes the branch the reader's own key takes: the quote
  // selector as the menu's Save quotation opens it, the menu as a Confirm release.
  if (shortcut == ReaderShortcut::Quote && !endOfBookMenuOpen) {
    openDictionaryWordSelect(true);
    return;
  }
  if (shortcut == ReaderShortcut::Menu && !endOfBookMenuOpen) confirmReleased = true;
  if (confirmLongPressed) {
    switch (SETTINGS.longPressMenuFunction) {
      case CrossPointSettings::LP_MENU_BOOKMARK:
        if (waitsForIndex()) break;
        addBookmark();
        showBookmarkMessage = true;
        bookmarkMessageTime = millis();
        requestUpdate();
        break;
      case CrossPointSettings::LP_MENU_KOSYNC:
        if (launchKOReaderSync()) {
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_DICTIONARY:
        openDictionaryWordSelect();
        return;
      case CrossPointSettings::LP_MENU_READER_MENU:
        // The hold swallowed its release; stand in for it so the menu opens
        // through the same branch a short press takes.
        confirmReleased = true;
        break;
      case CrossPointSettings::LP_MENU_FILE_TRANSFER:
        activityManager.goToFileTransfer();
        return;
      case CrossPointSettings::LP_MENU_TILT_PAGE_TURN:
        toggleTiltFromReader();
        return;
      case CrossPointSettings::LP_MENU_SAVE_QUOTE:
        // What the reader menu's Save quotation runs: the quote selector on this page.
        openDictionaryWordSelect(true);
        return;
      case CrossPointSettings::LP_MENU_DISABLED:
      default:
        break;
    }
  }

  // Home-key boards (touch boards with a capacitive key; X3 and X4 have none) run the action
  // configured for the Home tap, double tap or hold.
  if (!endOfBookMenuOpen) {
    switch (mappedInput.homeButtonAction()) {
      case HomeButtonAction::Bookmark:
        if (!showBookmarkMessage && !waitsForIndex()) {
          addBookmark();
          showBookmarkMessage = true;
          bookmarkMessageTime = millis();
          requestUpdate();
        }
        return;
      case HomeButtonAction::Sync:
        launchKOReaderSync();
        return;
      case HomeButtonAction::Dictionary:
        if (!showDictionaryMessage) openDictionaryWordSelect();
        return;
      case HomeButtonAction::ReaderMenu:
        if (usesToolbarMenu() && section)
          openOverlay(Overlay::Toolbar);
        else
          openReaderMenu();
        return;
      default:
        break;
    }
  }

  // Link taps take priority over the reader-menu and page-turn zones.
  if (!atEndOfBook && !currentPageLinks.empty() && SETTINGS.touchReaderControls && mappedInput.hasTouch()) {
    int touchX = 0;
    int touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      const auto* link = EpubReaderUtils::linkAtPoint(currentPageLinks, touchX, touchY, currentPageLinkMarginLeft,
                                                      currentPageLinkMarginTop);
      if (link) {
        navigateToHref(link->href, true);
        return;
      }
    }
  }

  // Touch shell: the top band opens the top menu, the foot band (title, clock, battery) the text menu.
  // A stroke up from the foot band shorter than a swipe (60 px) is a tap there: it opens the text menu and
  // never leaves the book; leaving takes a whole swipe up (ActivityManager, wasBottomHomeGesture).
  if (tenorchrome::kTouchShell) {
    switch (ReaderUtils::tapZone(renderer, mappedInput, false, true)) {
      case readertap::Zone::TopMenu:
        activityManager.openTopMenu();
        return;
      case readertap::Zone::TextMenu:
        if (usesToolbarMenu() && section) {
          focusedTool = 1;  // the toolbar's Text tool
          openOverlay(Overlay::Text);
        } else {
          openReaderMenu();
        }
        return;
      default:
        break;
    }
  }

  if (confirmReleased || ReaderUtils::isTouchMenuGesture(renderer, mappedInput, tenorchrome::kTouchShell)) {
    // Toolbar style: the page is on screen and in the framebuffer, so paint the
    // toolbar over it (one refresh) instead of pushing a full-screen menu.
    if (usesToolbarMenu() && section) {
      pendingManualTurn = 0;
#ifdef TENOR_TURN_TRACE
      dropTurnTrace(pendingManualTurnTrace, "toolbar");
#endif
      openOverlay(Overlay::Toolbar);
    } else {
      openReaderMenu();
    }
  }

  if (footnoteDepth > 0 && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
    restoreSavedPosition();
    return;
  }

  if (handleBackNavigation()) {
    return;
  }

  if ((!endOfBookMenuOpen && mappedInput.homeButtonAction() == HomeButtonAction::Footnotes) ||
      (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FOOTNOTES &&
       mappedInput.wasReleased(MappedInputManager::Button::Power) &&
       !mappedInput.wasReleased(MappedInputManager::Button::Down))) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
    } else {
      openFootnoteSelect(false);
    }
    return;
  }

  if (processExternalPageTurn()) return;

  const bool turnGuardActive = RenderLock::peek() || !manualPageTurnReady();

  // Turbo giữ nút: sau nắc chương đầu, nút còn giữ thì nắc tiếp theo nhịp
  // cố định tới khi thả. Cả hai hướng cùng giữ thì dừng (không định hướng).
  // Paint đang bay thì dời nắc sang tick sau, giữ nguyên hẹn. Kiểm cả nút
  // bên (PageBack/PageForward) lẫn nút front (Left/Right) như detectPageTurn.
  if (turboHoldDirection != 0) {
    const bool toGiu = turboHoldDirection > 0
        ? (mappedInput.isPressed(MappedInputManager::Button::PageForward) ||
           mappedInput.isPressed(MappedInputManager::Button::Right))
        : (mappedInput.isPressed(MappedInputManager::Button::PageBack) ||
           mappedInput.isPressed(MappedInputManager::Button::Left));
    const bool nguocGiu = turboHoldDirection > 0
        ? (mappedInput.isPressed(MappedInputManager::Button::PageBack) ||
           mappedInput.isPressed(MappedInputManager::Button::Left))
        : (mappedInput.isPressed(MappedInputManager::Button::PageForward) ||
           mappedInput.isPressed(MappedInputManager::Button::Right));
    if (!toGiu || nguocGiu) {
      turboHoldDirection = 0;
    } else if (millis() >= turboNextJumpMs && !turnGuardActive) {
      if (!nhayChuongMotBac(turboHoldDirection, std::nullopt) && !skipPages(turboHoldDirection)) {
        turboHoldDirection = 0;
      }
      turboNextJumpMs = millis() + ReaderUtils::SKIP_HOLD_MS;
      requestUpdate();
    }
  }
  if (pendingManualTurn != 0 && !turnGuardActive && !pageAwaitsLayout()) {
    if (!section && !xemTruoc) {
      pendingManualTurn = 0;
#ifdef TENOR_TURN_TRACE
      dropTurnTrace(pendingManualTurnTrace, "no_section");
#endif
      return;
    }
    const bool forward = pendingManualTurn > 0;
    int8_t remaining = pendingManualTurn;
    pendingManualTurn = 0;
#ifdef TENOR_TURN_TRACE
    currentTurnTrace = pendingManualTurnTrace;
    pendingManualTurnTrace = {};
#endif
    // Presses queued during a paint land together: one repaint shows the page
    // they add up to. No return after: a press read in this same pass is queued
    // behind them below instead of being dropped.
    bool changed = false;
    while (remaining != 0 && !isAtEndOfBook() && pageTurn(forward)) {
      changed = true;
      remaining -= forward ? 1 : -1;
      if (pageAwaitsLayout() || !section) break;
    }
    // A turn into the next chapter leaves no section until it is laid out, and a turn onto a
    // page not laid out yet waits for the paint that lays it out; the rest waits for either.
    if (changed && (!section || pageAwaitsLayout())) pendingManualTurn = remaining;
    if (changed) requestUpdate();
  }
  if (paintDropped && pendingManualTurn == 0 && pendingExternalTurn == 0 && !RenderLock::peek()) {
    paintDropped = false;
    requestUpdate();
  }

  const auto turns = ReaderUtils::detectPageTurn(mappedInput);
  const bool prevPageTriggered = turns.prev || touch.prev;
  const bool nextPageTriggered = turns.next || touch.next;
  const bool prevTriggered = prevPageTriggered || turns.prevLongPressed;
  const bool nextTriggered = nextPageTriggered || turns.nextLongPressed;
  if (!prevTriggered && !nextTriggered) {
    return;
  }

#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("ERS_TRACE", "INPUT_TRIGGER t=%lu spine=%d page=%d count=%u prev=%u next=%u prev_long=%u next_long=%u touch=%u heap=%u largest=%u min=%u",
          millis(), currentSpineIndex, section ? section->currentPage : nextPageNumber,
          section ? static_cast<unsigned>(section->pageCount) : 0u, prevTriggered ? 1u : 0u,
          nextTriggered ? 1u : 0u, turns.prevLongPressed ? 1u : 0u, turns.nextLongPressed ? 1u : 0u,
          (touch.prev || touch.next) ? 1u : 0u, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()));
#endif

  if (SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP) {
    if (turns.prevButtonPressed) rememberChapterHoldOrigin(-1);
    if (turns.nextButtonPressed) rememberChapterHoldOrigin(1);
  }

  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) {
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    return;
  }

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool longPress =
      !turns.fromTilt &&
      (turns.prevLongPressed || turns.nextLongPressed ||
       ((touch.prev || touch.next) && heldMs >= ReaderUtils::SKIP_HOLD_MS));
  // Giu nut lat trang = Co chu: mot lan giu, mot nac, KEP o bien; cu release da bi
  // wasLongPressed nuot nen khong lat trang du. Nghieng (fromTilt) va nut nguon khong toi day.
  if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.FONT_SIZE_STEP) {
    if (docCoChuMotNac(nextTriggered ? 1 : -1)) requestUpdate();
    return;
  }

  if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP) {
    // Giu nut = doi DUNG MOT chuong theo muc luc; nha nut sau do khong sinh luot lat
    // them vi wasLongPressed da nuot luot release (xem detectPageTurn). Sach khong co
    // muc luc thi giu nguyen kha nang cu: nhay mot doan trang. Nút bên cạnh còn giữ
    // sau nắc này tiếp tục nắc theo nhịp (turbo) tới khi thả.
    const bool fromTouchHold = touch.prev || touch.next;
    const int huong = (fromTouchHold ? touch.next : turns.nextLongPressed) ? 1 : -1;
    const auto& holdOrigin = huong > 0 ? chapterHoldNextOrigin : chapterHoldPrevOrigin;
    // A touch hold fires on release, so its current position is the origin.
    // Stored button origins belong to earlier presses and can be stale here.
    const std::optional<int> logicalOrigin =
        !fromTouchHold && holdOrigin.has_value()
            ? std::optional<int>(logicalTocIndexForPosition(*holdOrigin, huong))
            : std::nullopt;
    if (huong > 0)
      chapterHoldNextOrigin.reset();
    else
      chapterHoldPrevOrigin.reset();
    if (!nhayChuongMotBac(huong, logicalOrigin)) skipPages(huong);
    if (!fromTouchHold) {
      // Chỉ bật turbo cho nút vật lý: giữ nút vẫn còn sau nắc thì nắc tiếp.
      turboHoldDirection = static_cast<int8_t>(huong);
      turboNextJumpMs = millis() + ReaderUtils::SKIP_HOLD_MS;
    }
    requestUpdate();
    return;
  }

  if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.ORIENTATION_CHANGE) {
    const uint8_t newOrientation =
        nextTriggered ? (SETTINGS.orientation - 1 + SETTINGS.ORIENTATION_COUNT) % SETTINGS.ORIENTATION_COUNT
                      : (SETTINGS.orientation + 1) % SETTINGS.ORIENTATION_COUNT;
    applyOrientation(newOrientation);
    requestUpdate();
    return;
  }

  if (!section && !xemTruoc) {
    requestUpdate();
    return;
  }

#ifdef TENOR_TURN_TRACE
  currentTurnTrace = detectTurnTrace((touch.prev || touch.next) ? "touch" : turns.fromTilt ? "tilt" : "button",
                                     !prevPageTriggered);
#endif
  // Anything still queued goes first, so a new press joins the queue behind it, and so does a
  // press while the page on screen still waits for its layout.
  if (turnGuardActive || pendingManualTurn != 0 || pageAwaitsLayout()) {
    pendingManualTurn = static_cast<int8_t>(
        std::clamp<int>(pendingManualTurn + (prevTriggered ? -1 : 1), -MAX_QUEUED_TURNS, MAX_QUEUED_TURNS));
#ifdef TENOR_TURN_TRACE
    replaceQueuedTurnTrace(pendingManualTurnTrace, currentTurnTrace, "manual_guard", true);
    currentTurnTrace = {};
    if (pendingManualTurn == 0) dropTurnTrace(pendingManualTurnTrace, "cancelled");
#endif
    return;
  }

  if (pageTurn(!prevPageTriggered)) requestUpdate();
}

void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) return;
  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) return;

  percent = clampPercent(percent);

  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) targetSize = bookSize - 1;

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) return;

  const int found = epub->getSpineIndexForSize(targetSize);
  const int targetSpineIndex = found >= 0 ? found : spineCount - 1;
  const size_t prevCumulative = found > 0 ? epub->getCumulativeSpineItemSize(found - 1) : 0;

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  pendingSpineProgress = std::clamp(pendingSpineProgress, 0.0f, 1.0f);

  {
    RenderLock lock;
    clearDeferredReposition();
    currentSpineIndex = targetSpineIndex;
    TRACE_JUMP_BEGIN("percent");
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::onReaderMenuConfirm(const EpubReaderMenuActivity::MenuAction action, const MenuResult& menu) {
  // The chapter list, percent jump, sync and bookmarks read the TOC or the chapter sizes.
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER:
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT:
    case EpubReaderMenuActivity::MenuAction::SYNC:
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS:
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK:
      if (waitsForIndex()) return;
      break;
    default:
      break;
  }
  auto progressChangeResultHandler = [this](const ActivityResult& result) {
    loadCachedBookmarks();
    if (result.isCancelled) {
      openReaderMenu();
    } else {
      const auto& sync = std::get<ProgressChangeResult>(result.data);

      if (sync.hasVisibleTextOffset && sync.spineIndex >= 0 && sync.spineIndex < epub->getSpineItemsCount()) {
        RenderLock lock;
        clearDeferredReposition();
        if (section && currentSpineIndex == sync.spineIndex) {
          const auto page = section->getPageForVisibleTextOffset(sync.visibleTextOffset);
          section->currentPage = page.value_or(std::max(0, sync.page));
        } else {
          currentSpineIndex = sync.spineIndex;
          pendingOffsetJump = sync.visibleTextOffset;
          nextPageNumber = std::max(0, sync.page);
          section.reset();
        }
        requestUpdate();
        return;
      }

      int targetSpineIndex = sync.spineIndex;
      int targetPage = sync.page;
      const int activeTotalPages = section ? section->estimatedTotalPages() : 0;
      const bool cachedPageMatchesActiveSection = section && sync.totalPages > 0 &&
                                                  currentSpineIndex == sync.spineIndex && sync.page >= 0 &&
                                                  sync.page < sync.totalPages && activeTotalPages == sync.totalPages;

      if (!cachedPageMatchesActiveSection && sync.hasSavedProgress) {
        const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
        CrossPointPosition fallback =
            ProgressMapper::toCrossPoint(epub, {sync.xpath, sync.percentage}, renderer, currentSpineIndex, totalPages);
        targetSpineIndex = fallback.spineIndex;
        targetPage = fallback.pageNumber;
      }

      RenderLock lock;
      clearDeferredReposition();

      if (currentSpineIndex != targetSpineIndex) {
        currentSpineIndex = targetSpineIndex;
        TRACE_JUMP_BEGIN("bookmark");
        nextPageNumber = targetPage;
        section.reset();
      } else if (section && section->currentPage != targetPage) {
        const int clampedTargetPage = std::max(0, targetPage);
        section->currentPage = clampedTargetPage;
      } else if (!section) {
        nextPageNumber = targetPage;
      }
      requestUpdate();
    }
  };

  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      // Release the section while the chapter list is up (mirrors the
      // TEXT_SETTINGS path): picking a chapter resets it anyway, and its
      // tens-of-KB footprint is the difference between the chapter list
      // holding its CJK glyph arena (RAM-only repaints) and re-reading
      // glyphs from SD on every row step. Cancel restores via the same
      // cached-position rebuild TEXT_SETTINGS uses.
      {
        RenderLock lock;
        if (section) {
          rememberCurrentContentOffset();
          cachedSpineIndex = currentSpineIndex;
          cachedChapterTotalPageCount = section->pageCount;
          nextPageNumber = section->currentPage;
        }
        section.reset();
      }
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, spineIdx),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
              return;
            }
            const auto& chapterResult = std::get<ChapterResult>(result.data);
            RenderLock lock;
            clearDeferredReposition();
            currentSpineIndex = chapterResult.spineIndex;
            TRACE_JUMP_BEGIN("toc");
            pendingAnchor = chapterResult.anchor;
            nextPageNumber = 0;
            section.reset();
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      openFootnoteSelect(true);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS:
    case EpubReaderMenuActivity::MenuAction::FONT_SIZE:
    case EpubReaderMenuActivity::MenuAction::FONT_FAMILY: {
      // Menu da chon xong tai cho (trinh chon hoac doi tai cho): ap MOT lan, dan lai mot lan,
      // luu ngoai khoa. Khong mo man nao.
      if (action == EpubReaderMenuActivity::MenuAction::FONT_SIZE && menu.coChu > 0) {
        if (menu.coChu == SETTINGS.fontPointSize) break;
        {
          RenderLock lock;
          fontdoc::apCo(renderer, menu.coChu);
          danLaiTrang();
        }
        SETTINGS.saveToFile();
        break;
      }
      if (action == EpubReaderMenuActivity::MenuAction::FONT_FAMILY && menu.hoFont >= 0) {
        bool daDoi = false;
        {
          RenderLock lock;
          daDoi = fontdoc::apHo(renderer, &sdFontSystem.registry(), menu.hoFont);
          if (daDoi) danLaiTrang();
        }
        if (daDoi) SETTINGS.saveToFile();
        break;
      }
      const auto tab = action == EpubReaderMenuActivity::MenuAction::FONT_SIZE     ? TextSettingsActivity::Tab::Size
                       : action == EpubReaderMenuActivity::MenuAction::FONT_FAMILY ? TextSettingsActivity::Tab::Family
                                                                                   : TextSettingsActivity::Tab::Layout;
      const AnhChupChu truoc = AnhChupChu::chup();
#ifdef TENOR_TURN_TRACE
      const unsigned long registryStarted = millis();
#endif
      // The font list: after a wake it reads every family folder on the card the first time.
      const auto* registry = &sdFontSystem.registry();
#ifdef TENOR_TURN_TRACE
      LOG_INF("ERS", "TEXT_SETTINGS registry=%lu", millis() - registryStarted);
#endif
      // The menu's own screen: its pause keeps the stats in RAM as the menu's did (274 ms on the X3).
      pauseKeepsStatsInRam = true;
      startActivityForResult(
          std::make_unique<TextSettingsActivity>(renderer, mappedInput, registry, tab),
          [this, truoc](const ActivityResult&) {
            if (truoc == AnhChupChu::chup()) return;
            RenderLock lock;
            danLaiTrang();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::NIGHT_MODE:
      // Handled in-place by EpubReaderMenuActivity so its On/Off value updates
      // without closing the menu.
      break;
    case EpubReaderMenuActivity::MenuAction::FRONTLIGHT:
      // Handled in-place by EpubReaderMenuActivity using the live frontlight HAL.
      break;
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      const int initialPercent = bookPercentFor(chapterPosition());
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
            } else {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SAVE_QUOTE:
      openDictionaryWordSelect(true);
      break;
    case EpubReaderMenuActivity::MenuAction::QUOTES_OF_BOOK:
      openBookQuotes();
      break;
    case EpubReaderMenuActivity::MenuAction::DICTIONARY: {
      openDictionaryWordSelect();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        std::string fullText = section->getTextFromSectionFile();
        if (!fullText.empty()) {
          startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                                 [this](const ActivityResult&) { openReaderMenu(); });
          break;
        }
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock;
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          section.reset();
          epub->clearCache();
          epub->setupCacheDir();
          if (!saveProgress(backupSpine, backupPage, backupPageCount)) {
            LOG_ERR("ERS", "Failed to save progress before cache clear");
          }
        }
      }
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock;
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      launchKOReaderSync();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BLUETOOTH: {
      startActivityForResult(std::make_unique<BlePageTurnerActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS: {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, epub, epub->getPath()),
          progressChangeResultHandler);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK: {
      addBookmark();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FILE_TRANSFER:
      // Progress is saved on every page paint, so leaving here loses nothing,
      // the same as Go home.
      activityManager.goToFileTransfer();
      return;
    case EpubReaderMenuActivity::MenuAction::TILT_PAGE_TURN:
      // The list menu toggles in place; this is the toolbar's More panel.
      toggleTiltFromReader();
      break;
  }
}

unsigned long EpubReaderActivity::confirmLongPressThreshold() const {
  switch (SETTINGS.longPressMenuFunction) {
    case CrossPointSettings::LP_MENU_BOOKMARK:
    case CrossPointSettings::LP_MENU_DICTIONARY:
    case CrossPointSettings::LP_MENU_READER_MENU:
    case CrossPointSettings::LP_MENU_FILE_TRANSFER:
    case CrossPointSettings::LP_MENU_TILT_PAGE_TURN:
    case CrossPointSettings::LP_MENU_SAVE_QUOTE:
      return ReaderUtils::BOOKMARK_HOLD_MS;
    case CrossPointSettings::LP_MENU_KOSYNC:
      return KOREADER_STORE.hasCredentials() ? ReaderUtils::GO_HOME_MS : 0;
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return 0;
  }
}

void EpubReaderActivity::toggleTiltFromReader() {
  SETTINGS.toggleTiltPageTurn();
  SETTINGS.saveToFile();
  // Borrow the bookmark popup: same place, same timeout, one line of state.
  tiltMessage = true;
  showBookmarkMessage = true;
  bookmarkMessageTime = millis();
  requestUpdate();
}

bool EpubReaderActivity::launchKOReaderSync() {
  if (!KOREADER_STORE.hasCredentials()) return false;
  if (waitsForIndex()) return true;

  RenderLock renderLock;

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;

  CrossPointPosition localPos = getCurrentPosition();
  SavedProgressPosition localKoPos;
  const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
  std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";
  const std::string savedEpubPath = epub->getPath();

  if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
    LOG_ERR("KOSync", "Aborting sync because current progress could not be saved");
    pendingSyncSaveError = true;
    requestUpdate();
    return true;
  }

  LOG_DBG("KOSync", "Releasing epub for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
  {
    if (section) {
      nextPageNumber = section->currentPage;
    }
    discardOverlayPage();
    ImageBlock::releaseRenderCache();
    ImageBlock::setExtractor(nullptr, nullptr);
    ImageBlock::setThumbHook(nullptr);
    section.reset();
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->releaseSdFontCaches();
    }
    // No rendering may run while the chapter mapper borrows the framebuffer.
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      localKoPos = ProgressMapper::toSavedProgress(epub, localPos);
    }
    // The destructor can no longer save the back-stack once epub is gone.
    saveLinkStack();
    epub.reset();
  }
  LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());

  activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
      renderer, mappedInput, savedEpubPath, localPos, std::move(localKoPos), std::move(localChapterName)));
  return true;
}

void EpubReaderActivity::applyInitialOrientation() {
  ReaderActivity::applyInitialOrientation();
  appliedOrientation = SETTINGS.orientation;
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // Also runs when SETTINGS already holds the new value but this layout was
  // built for the old one - that is what an external change looks like here.
  if (SETTINGS.orientation == orientation && appliedOrientation == orientation) {
    return;
  }

  RenderLock lock(*this);
  if (section) {
    rememberCurrentContentOffset();
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->pageCount;
    nextPageNumber = section->currentPage;
  }

  if (SETTINGS.orientation != orientation) {
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();
  }
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  appliedOrientation = orientation;
  section.reset();
  dropCatchUp();  // laid out for the old viewport; the next one starts under the new
}

void EpubReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  if (selectedPageTurnOption == 0 || selectedPageTurnOption >= std::size(PAGE_TURN_RATES)) {
    automaticPageTurnActive = false;
    return;
  }

  lastPageTurnTime = millis();
  pageTurnDuration = (1UL * 60 * 1000) / PAGE_TURN_RATES[selectedPageTurnOption];
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = readerStatusBarHeight();
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    RenderLock lock;
    if (section) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
    dropCatchUp();  // laid out for the old page height
  }
}

bool EpubReaderActivity::latTrangThat(bool isForwardTurn) {
  if (!section) {
    if (!xemTruoc) return false;
    xemTruocLat = isForwardTurn ? 1 : -1;  // the paint lays the chapter out to the preview, then turns
    deferredClearPending.store(true, std::memory_order_release);
    lastPageTurnTime = millis();
    return true;
  }
  if (xemTruocTrenMan) {
    xemTruocTrenMan = false;
    if (!isForwardTurn) {  // the landed page holds the lines above the preview
      deferredClearPending.store(true, std::memory_order_release);
      lastPageTurnTime = millis();
      return true;
    }
  }
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("ERS_TRACE", "PAGE_TURN_ATTEMPT t=%lu spine=%d page=%d count=%u dir=%u building=%u heap=%u largest=%u",
          millis(), currentSpineIndex, section->currentPage, static_cast<unsigned>(section->pageCount),
          isForwardTurn ? 1u : 0u, section->isBuilding() ? 1u : 0u,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  // ReaderActivity owns the render lock for every page mutation, including
  // chapter changes. The lock is nonrecursive.
  deferredClearPending.store(true, std::memory_order_release);
  if (isForwardTurn) {
    if (section->currentPage < section->pageCount - 1 || section->isBuilding() || section->isPartial()) {
      section->currentPage++;
      lastPageTurnTime = millis();
      return true;
    } else if (currentSpineIndex + 1 < epub->getSpineItemsCount()) {
      nextPageNumber = 0;
      currentSpineIndex++;
      section.reset();
      lastPageTurnTime = millis();
      return true;
    } else {
      currentSpineIndex = epub->getSpineItemsCount();
      lastPageTurnTime = millis();
      return true;
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
      lastPageTurnTime = millis();
      return true;
    } else if (currentSpineIndex > 0) {
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      currentSpineIndex--;
      section.reset();
      lastPageTurnTime = millis();
      return true;
    }
  }
  return false;
}

int EpubReaderActivity::logicalTocIndexForPosition(const ChapterHoldOrigin& origin, const int huong) {
  if (!epub) return -1;
  const int soMuc = static_cast<int>(epub->getTocItemsCount());
  if (soMuc <= 0) return -1;
  const int originSpineIndex = origin.spineIndex;
  // Muc dang o = muc cuoi cung co spine khong vuot qua spine dang doc. Mot XHTML co the
  // chua nhieu muc, nen buoc nhay di theo TUNG MUC chu khong theo tung tep XHTML.
  // Khoang muc TOC cua spine goc chi tinh mot lan cho moi spine (xem tocSpineCached):
  if (tocSpineCached != originSpineIndex) {
    int dau = -1, cuoi = -1, truoc = -1, tu = 0;
    // A later spine picks up after the range already found: every entry up to it points at a
    // spine before this one (the scan stops at the first later spine, so entries run in spine
    // order). A turbo hold used to read the TOC from entry 0 on every step.
    if (tocSpineCached >= 0 && originSpineIndex > tocSpineCached) {
      truoc = std::max(tocCuoiTrongSpine, tocTruocTrongSpine);
      tu = truoc + 1;
    }
    int i = tu;
    for (; i < soMuc; i++) {
      const auto muc = epub->getTocItem(i);
      if (muc.spineIndex < 0) continue;
      if (muc.spineIndex < originSpineIndex) {
        truoc = i;
        continue;
      }
      if (muc.spineIndex == originSpineIndex) {
        if (dau < 0) dau = i;
        cuoi = i;
        continue;
      }
      break;
    }
#ifdef TENOR_PRESS_PROBE
    LOG_INF("ERS", "TOC_SCAN spine=%d from=%d items=%d", originSpineIndex, tu, i - tu);
#else
    LOG_DBG("ERS", "TOC_SCAN spine=%d from=%d items=%d", originSpineIndex, tu, i - tu);
#endif
    tocDauTrongSpine = dau;
    tocCuoiTrongSpine = cuoi;
    tocTruocTrongSpine = truoc;
    tocSpineCached = originSpineIndex;
  }
  const int dauTrongSpine = tocDauTrongSpine;
  const int cuoiTrongSpine = tocCuoiTrongSpine;
  // Muc cuoi cung nam TRUOC spine goc: moc de di lui khi chua biet vi tri trong spine.
  int dangO = tocTruocTrongSpine;
  // Mot tep XHTML co the chua nhieu muc TOC (nhieu neo). "Muc dang o" phai la muc XA NHAT
  // trong spine goc ma neo cua no van con nam TRUOC hoac NGAY TAI trang goc: nho vay
  // giu-nut-tiep di a1 -> a2 -> a3, giu-nut-lui di a3 -> a2 -> a1, va mo lai giua a2 thi
  // van tinh dung a2. Khi spine chi co mot muc thi ket qua y nguyen nhu truoc.
  if (dauTrongSpine >= 0) {
    dangO = dauTrongSpine;
    if (!origin.pendingAnchor.empty()) {
      for (int i = dauTrongSpine; i <= cuoiTrongSpine; ++i) {
        const auto muc = epub->getTocItem(i);
        if (muc.spineIndex == originSpineIndex && muc.anchor == origin.pendingAnchor) {
          dangO = i;
          break;
        }
      }
    } else if (section && currentSpineIndex == originSpineIndex) {
      const int trangDangDoc = origin.pageNumber;
      // Khong duoc gia dinh so trang cua cac muc TOC trong mot tep la khong giam: TOC co the khong
      // theo thu tu trang, va mot neo co the thieu/hong ngay GIUA danh sach (khong chi o sau).
      // Vi vay KHONG dung chat nhi phan. Quet nguoc tu muc cuoi va dung o muc dau tien co neo
      // giai duoc va trang <= trang dang doc: dung trong moi truong hop, ke ca TOC khong sap thu tu.
      // Neu khong muc nao thoa (hoac ca bang neo hong) thi lui ve muc dau spine.
      // Chi phi: O(so muc SAU muc dang o). Toc do that su den tu cache khoang TOC o tren (khong
      // con quet ca bang moi nhip), phan con lai ghi lai lam gioi han de review.
      for (int i = cuoiTrongSpine; i >= dauTrongSpine; i--) {
        const auto muc = epub->getTocItem(i);
        if (muc.spineIndex != originSpineIndex) continue;
        int trangNeo = 0;  // muc khong co neo = dau tep = trang 0
        if (!muc.anchor.empty()) {
          const auto trangTimDuoc = section->findAnchor(muc.anchor);
          if (!trangTimDuoc.has_value()) continue;  // chua giai duoc: khong ket luan duoc gi
          trangNeo = static_cast<int>(*trangTimDuoc);
        }
        if (trangNeo <= trangDangDoc) {
          dangO = i;
          break;
        }
      }
    } else if (currentSpineIndex != originSpineIndex) {
      // Neu lat trang vua vuot sang spine moi thi section cua spine goc da duoc tha.
      // Lui tu trang dau can moc dau, tien tu trang cuoi can moc cuoi.
      dangO = huong < 0 ? dauTrongSpine : cuoiTrongSpine;
    }
  }
  return dangO;
}

void EpubReaderActivity::rememberChapterHoldOrigin(const int huong) {
  ChapterHoldOrigin origin;
  origin.spineIndex = currentSpineIndex;
  origin.pageNumber = section ? section->currentPage : nextPageNumber;
  // pendingAnchor is only needed while a chapter target is still waiting for its section.
  // Keep the snapshot bounded so a malformed EPUB cannot make a short press copy an arbitrary
  // amount of metadata before the page turn.
  constexpr size_t kMaxHoldOriginAnchorLength = 128;
  if (pendingAnchor.size() <= kMaxHoldOriginAnchorLength) origin.pendingAnchor = pendingAnchor;
  if (huong > 0)
    chapterHoldNextOrigin = std::move(origin);
  else
    chapterHoldPrevOrigin = std::move(origin);
}

bool EpubReaderActivity::nhayChuongThat(const int huong) {
  // Giu nut tren remote BLE. Nguoi goi da giu khoa ve, nen KHONG goi skipPages o day
  // nhu duong giu nut vat ly: ham do tu lay khoa, long vao nhau la ket. Sach khong co
  // muc luc thi giu nut khong lam gi, va nguoi goi ghi mot dong noi vi sao.
  return nhayChuongMotBac(huong, std::nullopt, /*khoaDaGiu=*/true);
}

bool EpubReaderActivity::nhayChuongMotBac(int huong, std::optional<int> logicalOrigin, const bool khoaDaGiu) {
  if (!epub || huong == 0) return false;
  const int soMuc = static_cast<int>(epub->getTocItemsCount());
  if (soMuc <= 0) return false;
  int dangO = -1;
  if (logicalOrigin.has_value()) {
    dangO = *logicalOrigin;
  } else {
    ChapterHoldOrigin currentOrigin;
    currentOrigin.spineIndex = currentSpineIndex;
    currentOrigin.pageNumber = section ? section->currentPage : nextPageNumber;
    currentOrigin.pendingAnchor = pendingAnchor;
    dangO = logicalTocIndexForPosition(currentOrigin, huong);
  }
  for (int i = dangO + huong; i >= 0 && i < soMuc; i += huong) {
    if (i == dangO) continue;
    const auto muc = epub->getTocItem(i);
    if (muc.spineIndex < 0) continue;  // muc tro toi tep khong nam trong sach: bo qua
    {
      std::optional<RenderLock> khoa;
      if (!khoaDaGiu) {
        // A hold fires ~700 ms after the press whose page is still in its gray pass: that pass
        // stops for the jump instead of holding it (nextScreenWaiting).
        jumpWaiting.store(true, std::memory_order_release);
        khoa.emplace();
        jumpWaiting.store(false, std::memory_order_relaxed);
      }
      clearDeferredReposition();
      currentSpineIndex = muc.spineIndex;
      TRACE_JUMP_BEGIN("hold");
      pendingAnchor = muc.anchor;
      nextPageNumber = 0;
      section.reset();
    }
    requestUpdate();
    return true;
  }
  return false;  // da o chuong dau hoac chuong cuoi
}

bool EpubReaderActivity::skipPages(int amount) {
  if (!section) return false;
  if (amount > 0) {
    RenderLock lock;
    nextPageNumber = 0;
    currentSpineIndex++;
    section.reset();
    return true;
  } else {
    if (section->currentPage > 0) {
      section->currentPage = 0;
      return true;
    } else if (currentSpineIndex > 0) {
      RenderLock lock;
      nextPageNumber = 0;
      currentSpineIndex--;
      section.reset();
      return true;
    }
  }
  return false;
}

bool EpubReaderActivity::isAtEndOfBook() const { return epub && currentSpineIndex >= epub->getSpineItemsCount(); }

void EpubReaderActivity::onReturnFromEndOfBook() {
  if (epub && epub->getSpineItemsCount() > 0) {
    currentSpineIndex = epub->getSpineItemsCount() - 1;
    nextPageNumber = 0;
    pendingPageJump = std::numeric_limits<uint16_t>::max();
  }
}

bool EpubReaderActivity::backgroundBuildWanted() const {
  return section && section->isBuilding() &&
         (section->isPartial() || static_cast<int>(section->pageCount) < section->currentPage + BUILD_WINDOW_AHEAD);
}

bool EpubReaderActivity::skipLoopDelay() {
  // The main loop holds the render lock while querying this hint.
  return (backgroundBuildCanTick() && backgroundBuildWanted()) || (catchUp && catchUp->isBuilding() && catchUpCanTick());
}

#ifdef TENOR_TURN_TRACE
void EpubReaderActivity::traceBuildTickBegin(const char* source) const {
  LOG_DBG("ERS_TRACE", "BUILD_TICK_BEGIN t=%lu source=%s spine=%d page=%d count=%u heap=%u largest=%u",
          millis(), source, currentSpineIndex, section->currentPage, static_cast<unsigned>(section->pageCount),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

void EpubReaderActivity::traceBuildTickEnd(const char* source) const {
  LOG_DBG("ERS_TRACE", "BUILD_TICK_END t=%lu source=%s spine=%d page=%d count=%u building=%u complete=%u heap=%u largest=%u",
          millis(), source, currentSpineIndex, section->currentPage, static_cast<unsigned>(section->pageCount),
          section->isBuilding() ? 1u : 0u, section->isBuildComplete() ? 1u : 0u,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

void EpubReaderActivity::tracePaint(const char* phase, const char* kind) const {
  const unsigned long now = millis();
  LOG_INF("ERS_TRACE", "PAINT_%s rid=%u id=%u t=%lu elapsed_ms=%lu input_ms=%lu spine=%d page=%d kind=%s heap=%u largest=%u min=%u",
          phase, static_cast<unsigned>(paintTraceSequence), static_cast<unsigned>(appliedTurnTrace.id), now,
          now - paintTraceStarted, appliedTurnTrace.id == 0 ? 0UL : now - appliedTurnTrace.detectedMs,
          currentSpineIndex, section ? section->currentPage : nextPageNumber, kind,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          static_cast<unsigned>(ESP.getMinFreeHeap()));
}

void EpubReaderActivity::traceReadablePaint(const char* kind) {
  if (readablePaintTraced) return;
  readablePaintTraced = true;
  // This is a software upper bound after an existing completion boundary.
  // Physical pixel timing still requires a synchronized panel recording.
  tracePaint("READABLE_BOUND", kind);
}
#endif

void EpubReaderActivity::renderBook() {
  // A build popup still refreshing when this paint ends is waited out before anything else draws.
  struct PopupSettle {
    EpubReaderActivity& reader;
    ~PopupSettle() { reader.settleBuildPopup(); }
  } popupSettle{*this};
#ifdef TENOR_TURN_TRACE
  ++paintTraceSequence;
  paintTraceStarted = millis();
  readablePaintTraced = false;
  tracePaint("BEGIN", deferBackgroundBuildForBle() ? "ble_deferred" : "book");
#endif
#ifdef TENOR_UI_ACCEPTANCE
#ifndef SIMULATOR
  LOG_INF("ERS", "Render CPU=%uMHz heap=%u", getCpuFrequencyMhz(), ESP.getFreeHeap());
#else
  LOG_INF("ERS", "Render simulator heap=%u", ESP.getFreeHeap());
#endif
#endif
  pageFrameShown = false;
  fontsShedForBuild = false;
  currentPageLinks.clear();
  // Runs under the render task's RenderLock; catches every requestUpdate()
  // exit from the overlay while its deferred chrome refresh is still pending,
  // before anything below touches the framebuffer.
  settleOverlayRefresh();
  takePendingDeferredClear();  // page turns queue this instead of waiting for the lock
  if (!epub) return;
  // Read before the layout below settles it: a turn stepped past the pages laid out so far.
  const bool turnPastLaidOut = pageAwaitsLayout();

  const auto showPendingSyncSaveError = [this]() {
    if (!pendingSyncSaveError) return;
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  };

  const auto showBuildError = [this]() {
#ifdef TENOR_TURN_TRACE
    tracePaint("ERROR", "index");
    logTurnTrace("FAILED", appliedTurnTrace, "index");
    appliedTurnTrace = {};
#endif
    settleBuildPopup();
    renderer.clearScreen();
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawPopup(renderer, tr(STR_INDEX_FAILED));
    automaticPageTurnActive = false;
    // A reselection waits for its own page only; after a failed build the next page drawn is
    // wherever the reader moves to, so the selector must not open over it.
    pendingQuoteEdit.clear();
  };

  if (currentSpineIndex < 0) currentSpineIndex = 0;
  if (currentSpineIndex > epub->getSpineItemsCount()) currentSpineIndex = epub->getSpineItemsCount();

  if (currentSpineIndex == epub->getSpineItemsCount()) {
    pendingQuoteEdit.clear();
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  readingMargins(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  const uint8_t statusBarHeight = readerStatusBarHeight();

  if (automaticPageTurnActive &&
      (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    orientedMarginBottom +=
        std::max(SETTINGS.screenMargin,
                 static_cast<uint8_t>(statusBarHeight + UITheme::getInstance().getMetrics().statusBarVerticalMargin)) -
        std::max(SETTINGS.screenMargin, statusBarHeight);
  }

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  buildViewportWidth = viewportWidth;
  buildViewportHeight = viewportHeight;

  const ReaderRenderSpec renderSpec = SETTINGS.readerRenderSpec(viewportWidth, viewportHeight);

  if (xemTruoc && !section) {
    const bool samePlace = currentSpineIndex == cachedSpineIndex && pendingAnchor.empty() && !pendingPageJump &&
                           !pendingPercentJump && !pendingOffsetJump;
    if (samePlace && xemTruocLat == 0 &&
        renderPreview(orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft))
      return;
    // A turn from the preview, a jump, or no preview to draw: the chapter is laid out here. What the
    // catch-up laid out is kept as a partial (the destructor suspends it) and the build below resumes it.
    if (samePlace) {
      catchUp.reset();
      pendingOffsetJump = xemTruocDich;  // honoured over a page number, also by a cache already laid out
    } else {
      dropCatchUp();
      xemTruocLat = 0;
    }
    xemTruoc = false;
  }

  if (!section) {
#ifdef TENOR_PRESS_PROBE
    const unsigned long jumpBuildStarted = millis();
    const bool jumping = pendingPercentJump || !pendingAnchor.empty() || pendingOffsetJump || pendingPageJump;
#endif
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer, preview));
    partialRebuildStartFailed = false;
    backgroundBuildSuspended = false;
    buildHeapPaused = false;

    const bool cacheLoaded = section->loadSectionFile(renderSpec);
    if (cacheLoaded) {
      cachedChapterTotalPageCount = 0;
      cachedVisibleTextOffset.reset();
    }
    const bool cacheComplete = cacheLoaded && !section->isPartial();
    const bool explicitOffsetJump = pendingOffsetJump.has_value();
    const std::optional<uint32_t> offsetJump =
        explicitOffsetJump ? pendingOffsetJump
        : (pendingPageJump.has_value() || !pendingAnchor.empty() || currentSpineIndex != cachedSpineIndex)
            ? std::nullopt
            : cachedVisibleTextOffset;
    if (!cacheComplete) {
      if (section->isPartial()) {
        LOG_DBG("ERS", "Partial cache found (%d pages), resuming build...", section->pageCount);
      } else {
        LOG_DBG("ERS", "Cache not found, building...");
      }

      if (pendingPercentJump) {
        // Lay out only as far as the target share: the whole chapter first took seconds on a long
        // one, behind a popup. The popup now follows the same deadline as the other builds.
        const unsigned long buildStartMs = millis();
        bool completedBuildTick = false;
        buildPopupPending = true;
        if (!section->laidOutTo(pendingSpineProgress)) {
          bool started;
          {
            GfxRenderer::FrameBufferLoan loan(renderer);
            started = section->startBuild(renderSpec, [this] { showBuildPopup(renderer, pagesUntilFullRefresh); });
          }
          if (!started) {
            LOG_ERR("ERS", "Failed to start section build");
            section.reset();
            buildPopupPending = false;
            showBuildError();
            return;
          }
        }
        while (!section->isBuildComplete() && !section->laidOutTo(pendingSpineProgress)) {
          if (completedBuildTick && buildPopupPending && millis() - buildStartMs >= BUILD_POPUP_DEADLINE_MS) {
            showBuildPopup(renderer, pagesUntilFullRefresh);
          }
          if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
            if (section->buildStarved()) {
              if (releaseHeapForBuild()) continue;
              buildPopupPending = false;
              showMemoryError();
              stayAfterStarvedJump();
              return;
            }
            LOG_ERR("ERS", "Failed during incremental section build");
            section.reset();
            buildPopupPending = false;
            showBuildError();
            return;
          }
          completedBuildTick = true;
        }
        buildPopupPending = false;
      } else {
        const int target = pendingPageJump.has_value() ? *pendingPageJump : (nextPageNumber < 0 ? 0 : nextPageNumber);
        const bool anchorJump = !pendingAnchor.empty();

        // A remembered position (Back out of the chapter list) that the partial already holds needs
        // no build: starting one copied the whole partial file to lay out nothing new.
        if (section->isPartial() &&
            (anchorJump            ? section->getPageForAnchor(pendingAnchor).has_value()
             : offsetJump.has_value() ? section->coversVisibleTextOffset(*offsetJump)
                                      : target < static_cast<int>(section->pageCount))) {
          LOG_DBG("ERS", "Partial covers target %d of %d; deferring extension build", target, section->pageCount);
        } else {
          const size_t spineBytes =
              epub->getCumulativeSpineItemSize(currentSpineIndex) -
              (currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0);
          const bool willInflate = !section->hasHtmlCache();
          bool showPopup;
          if (anchorJump) {
            // With no section file loaded there is no anchor map to read: asking it opened a file
            // that is not there (X3 r30, the second "Failed to open" at JUMP_BEGIN).
            showPopup = !(cacheLoaded && section->findAnchor(pendingAnchor).has_value()) &&
                        spineBytes > BUILD_POPUP_BYTE_THRESHOLD;
          } else {
            const bool targetAvailable = target < static_cast<int>(section->pageCount);
            showPopup = !targetAvailable && ((spineBytes > BUILD_POPUP_BYTE_THRESHOLD && willInflate) ||
                                             target > BUILD_POPUP_PAGE_THRESHOLD);
          }
          if (showPopup) {
#ifdef TENOR_PRESS_PROBE
            LOG_INF("ERS", "BUILD_POPUP src=open spine=%d target=%d pages=%u", currentSpineIndex, target,
                    static_cast<unsigned>(section->pageCount));
#endif
            GUI.drawPopup(renderer, tr(STR_INDEXING));
            pagesUntilFullRefresh = 1;
          }
          buildPopupPending = !showPopup;
          // Under TTF heap pressure (PSRAM boards only: X3 and X4 register no TTF font, so this is
          // skipped there), shed every rebuildable font cache before the build; dropped glyphs
          // re-fault on demand after it.
          if (!renderer.getTtfFonts().empty()) {
            if (auto* fcm = renderer.getFontCacheManager()) {
              fcm->releaseSdFontCaches();
            }
          }
          const unsigned long buildStartMs = millis();
          bool started;
          {
#ifdef TENOR_UI_ACCEPTANCE
            LOG_DBG("ERS_TRACE", "BUILD_START_BEGIN t=%lu source=foreground spine=%d target=%d count=%u spine_bytes=%u heap=%u largest=%u min=%u",
                    millis(), currentSpineIndex, target, static_cast<unsigned>(section->pageCount),
                    static_cast<unsigned>(spineBytes), static_cast<unsigned>(ESP.getFreeHeap()),
                    static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()));
#endif
            GfxRenderer::FrameBufferLoan loan(renderer);
            started = section->startBuild(renderSpec, [this] { showBuildPopup(renderer, pagesUntilFullRefresh); });
          }
#ifdef TENOR_UI_ACCEPTANCE
          LOG_DBG("ERS_TRACE", "BUILD_START_END t=%lu source=foreground ok=%u count=%u building=%u spine_bytes=%u heap=%u largest=%u min=%u",
                  millis(), started ? 1u : 0u, static_cast<unsigned>(section->pageCount),
                  section->isBuilding() ? 1u : 0u, static_cast<unsigned>(spineBytes),
                  static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
                  static_cast<unsigned>(ESP.getMinFreeHeap()));
#endif
          if (!started) {
            LOG_ERR("ERS", "Failed to start section build");
            section.reset();
            buildPopupPending = false;
            showBuildError();
            return;
          }
          bool completedBuildTick = false;
          // An anchor jump looks for its anchor among the pages this build lays out. The section
          // file cannot hold it: it was looked up above, and the build does not write that file
          // until it completes. Asking the file after every tick opened it once per ~20 ms tick
          // (X3 r05: 94 pages in 6.5 s, "Failed to open" between every tick).
          while (!section->isBuildComplete() &&
                 (anchorJump               ? !section->findAnchorDuringBuild(pendingAnchor)
                  : offsetJump.has_value() ? !section->buildReachedVisibleTextOffset(*offsetJump)
                                           : static_cast<int>(section->pageCount) <= target)) {
            if (completedBuildTick && buildPopupPending && millis() - buildStartMs >= BUILD_POPUP_DEADLINE_MS) {
              showBuildPopup(renderer, pagesUntilFullRefresh);
            }
#ifdef TENOR_UI_ACCEPTANCE
            traceBuildTickBegin("foreground");
#endif
            if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
              if (section->buildStarved()) {
                if (releaseHeapForBuild()) continue;
                buildPopupPending = false;
                showMemoryError();
                stayAfterStarvedJump();
                return;
              }
              LOG_ERR("ERS", "Failed during incremental section build");
              section.reset();
              buildPopupPending = false;
              showBuildError();
              return;
            }
            completedBuildTick = true;
#ifdef TENOR_UI_ACCEPTANCE
            traceBuildTickEnd("foreground");
#endif
          }
          buildPopupPending = false;
        }
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (pendingPageJump.has_value()) {
      section->currentPage = *pendingPageJump;
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) section->currentPage = 0;
    }

    if (offsetJump.has_value()) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*offsetJump)) {
        section->currentPage = *offsetPage;
        clearDeferredReposition();
      }
    }
    // A turn from the preview: back is the page holding its first character (the lines above it),
    // forward the page after.
    if (xemTruocLat > 0) section->currentPage++;
    xemTruocLat = 0;
    if (explicitOffsetJump) {
      clearDeferredReposition();
    }
    pendingOffsetJump.reset();

    if (!pendingAnchor.empty()) {
      const auto page = section->findAnchor(pendingAnchor);
      // A file holding one chapter starts with it, so its first page is still the right place.
      const auto severalChapters = [&] {
        const int toc = epub->getTocIndexForSpineIndex(currentSpineIndex);
        return toc >= 0 && toc + 1 < epub->getTocItemsCount() &&
               epub->getTocItem(toc + 1).spineIndex == currentSpineIndex;
      };
      if (page) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      } else if (lastSavedSpineIndex >= 0 && severalChapters()) {
        // The chapter is laid out and its anchor is not in the map (a one-file book whose map
        // outgrew the heap). Its first page would be a guess, saved as the progress: the reader
        // stays on the page it last saved instead, and nothing is written.
        LOG_ERR("ERS", "Anchor '%s' not in the chapter's map; staying on the saved page", pendingAnchor.c_str());
        pendingAnchor.clear();
        if (lastSavedSpineIndex == currentSpineIndex) {
          section->currentPage = lastSavedPage;
        } else {
          currentSpineIndex = lastSavedSpineIndex;
          nextPageNumber = lastSavedPage;
          section.reset();
          requestUpdate();
          return;
        }
      }
      pendingAnchor.clear();
    }

    if (pendingPercentJump && section->pageCount > 0) {
      section->currentPage = section->pageAtFraction(pendingSpineProgress);
      pendingPercentJump = false;
    }
#ifdef TENOR_PRESS_PROBE
    if (jumping) {
      LOG_INF("ERS", "JUMP_BUILD ms=%lu spine=%d page=%d pages=%u partial=%u building=%u",
              static_cast<unsigned long>(millis() - jumpBuildStarted), currentSpineIndex, section->currentPage,
              static_cast<unsigned>(section->pageCount), section->isPartial() ? 1u : 0u,
              section->isBuilding() ? 1u : 0u);
      buildprobe::log("jump");
    }
#endif
  }

  if (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    const unsigned long buildStartMs = millis();
    bool completedBuildTick = false;
    buildPopupPending = true;
    while (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->isBuilding() && !section->startBuild(renderSpec)) {
        LOG_ERR("ERS", "Failed to start partial extension build");
        section.reset();
        buildPopupPending = false;
        showBuildError();
        return;
      }
      while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
        if (completedBuildTick && buildPopupPending && millis() - buildStartMs >= BUILD_POPUP_DEADLINE_MS) {
          showBuildPopup(renderer, pagesUntilFullRefresh);
        }
#ifdef TENOR_UI_ACCEPTANCE
        traceBuildTickBegin("watermark");
#endif
        if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
          if (section->buildStarved()) {
            if (releaseHeapForBuild()) continue;
            buildPopupPending = false;
            showMemoryError();
            return;
          }
          LOG_ERR("ERS", "Failed during incremental section build");
          section.reset();
          buildPopupPending = false;
          showBuildError();
          return;
        }
        completedBuildTick = true;
#ifdef TENOR_UI_ACCEPTANCE
        traceBuildTickEnd("watermark");
#endif
      }
    }
    buildPopupPending = false;
  }
  if (section->isBuilding()) {
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
#ifdef TENOR_UI_ACCEPTANCE
      traceBuildTickBegin("window");
#endif
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        if (section->buildStarved()) {
          if (releaseHeapForBuild()) continue;
          showMemoryError();
          return;
        }
        LOG_ERR("ERS", "Failed during incremental section build");
        section.reset();
        showBuildError();
        return;
      }
#ifdef TENOR_UI_ACCEPTANCE
      traceBuildTickEnd("window");
#endif
    }
  }

  // renderBook already owns RenderLock. Explicit targets may build past the cached watermark;
  // once available, release the parser for BLE or low heap before page loading and grayscale allocation.
  suspendBackgroundBuild();

  if (!section->isBuilding() && section->pageCount > 0 &&
      section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
    // The chapter ended right at the page a turn stepped onto: that turn goes on into the next
    // chapter (or the end of the book) as it would have from the last page, instead of showing
    // the last page again with the turn counted. A queued turn back then comes back here.
    if (turnPastLaidOut) {
      latTrangThat(true);
      requestUpdate();
      return;
    }
  }

  // A turn queued behind this one (or Back, or a jump) leads past the page just laid out. It goes
  // no further than its layout: loading it, warming its glyphs and drawing it only to drop it
  // before the panel cost ~400 ms a page when queued turns ran past the laid-out pages (X3 r03).
  if (const char* reason = nextScreenWaiting()) {
    LOG_DBG("ERS", "Paint dropped before display: %s", reason);
#ifdef TENOR_TURN_TRACE
    tracePaint("ABORT", reason);
    logTurnTrace("RENDERED", appliedTurnTrace, "dropped");
    appliedTurnTrace = {};
#endif
    paintDropped.store(true, std::memory_order_release);
    progressSaveDeferred.store(true, std::memory_order_release);
    lastRenderCompleteMs = millis();
    return;
  }

  applyDeferredReposition();

  settleBuildPopup();
  renderer.clearScreen();

  if (section->pageCount == 0) {
#ifdef TENOR_TURN_TRACE
    tracePaint("ERROR", "empty_chapter");
    logTurnTrace("FAILED", appliedTurnTrace, "empty_chapter");
    appliedTurnTrace = {};
#endif
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    pendingQuoteEdit.clear();
    showPendingSyncSaveError();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
#ifdef TENOR_TURN_TRACE
    tracePaint("ERROR", "bounds");
    logTurnTrace("FAILED", appliedTurnTrace, "bounds");
    appliedTurnTrace = {};
#endif
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    pendingQuoteEdit.clear();
    showPendingSyncSaveError();
    return;
  }

  updateBookmarkFlag();

  {
#ifdef TENOR_TURN_TRACE
    tracePaint("LOAD_BEGIN", section->isBuilding() ? "building" : "cache");
#endif
    auto p = section->loadPage(section->currentPage);
#ifdef TENOR_TURN_TRACE
    tracePaint("LOAD_END", p ? "ok" : "failed");
#endif
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD (attempt %u)", static_cast<unsigned>(pageLoadRetryCount + 1));
      automaticPageTurnActive = false;
      const bool giveUp = ++pageLoadRetryCount > MAX_PAGE_LOAD_RETRIES;
      if (giveUp) {
#ifdef TENOR_TURN_TRACE
        tracePaint("ERROR", "page_load");
        logTurnTrace("FAILED", appliedTurnTrace, "page_load");
        appliedTurnTrace = {};
#endif
        LOG_ERR("ERS", "Page load retry limit reached, aborting");
        pageLoadRetryCount = 0;
        renderer.clearScreen();
        renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
        renderer.displayBuffer();
        pendingQuoteEdit.clear();
        showPendingSyncSaveError();
        return;
      }
      // A failed SD read or page allocation can recover on the next render.
      // Retry the existing section first, then allow one cache rebuild per incident.
      // Carry the requested page across reset; nextPageNumber may still be the
      // position from when this chapter was opened.
      if (pageLoadRetryCount == 2) {
        nextPageNumber = section->currentPage;
        section->abandonBuild();
        section->clearCache();
        section.reset();
      }
      requestUpdate();
      showPendingSyncSaveError();
      return;
    }
    pageLoadRetryCount = 0;

    currentPageVisibleOffset = p->visibleTextOffset;
    currentPageFootnotes = std::move(p->footnotes);
    currentPageLinks = std::move(p->links);
    currentPageLinkMarginLeft = orientedMarginLeft;
    currentPageLinkMarginTop = orientedMarginTop;

    // The overlay and non-tiled grayscale renderer share the renderer's single
    // stored-BW slot. Release the old page snapshot before renderContents()
    // needs that slot, then snapshot the newly rendered page below.
    discardOverlayPage();

    const auto start = millis();
    paintDropped.store(false, std::memory_order_relaxed);
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    if (paintDropped) {
      // The next paint shows the page the queue leads to; it writes the progress, and anything
      // this paint would have drawn over the page (a notice, a screenshot) waits for it too.
      progressSaveDeferred.store(true, std::memory_order_release);
      lastRenderCompleteMs = millis();
#ifdef TENOR_TURN_TRACE
      logTurnTrace("RENDERED", appliedTurnTrace, "dropped");
      appliedTurnTrace = {};
#endif
      return;
    }
#ifdef TENOR_TURN_TRACE
    tracePaint("COMPLETE", readablePaintTraced ? "page" : "no_readable_bound");
    logTurnTrace("RENDERED", appliedTurnTrace, "page");
    appliedTurnTrace = {};
#endif
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    lastRenderCompleteMs = millis();
    if (!pendingQuoteEdit.empty()) quoteEditPageShown = true;
    // The page is on the panel: write the cover thumbnails its decode built, before the page
    // counts as ready and the page-turner radio starts.
    if (coverThumbs && static_cast<CoverThumbCapture&>(*coverThumbs).write()) pendingThumbCount = 0;
    // A laid-out page reached the panel: the open may now be committed (onTick, main task).
    markPageRendered();
    xemTruocTrenMan = false;
  }

  if (pageBeingLeft()) {
    progressSaveDeferred.store(true, std::memory_order_release);
  } else {
    saveProgressIfMoved();
  }

  showPendingSyncSaveError();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  if (showBookmarkMessage) {
    if (tiltMessage) {
      const std::string text = std::string(tr(STR_TILT_PAGE_TURN)) + ": " +
                               (SETTINGS.tiltPageTurn ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
      GUI.drawPopup(renderer, text.c_str());
    } else {
      GUI.drawPopup(renderer, bookmarkRemoved ? tr(STR_BOOKMARK_REMOVED) : tr(STR_BOOKMARK_ADDED));
    }
  }

  if (showDictionaryMessage) {
    GUI.drawPopup(renderer, tr(STR_DICT_NO_DICT_SET));
  }

  if (showIndexingMessage) {
    GUI.drawPopup(renderer, tr(STR_INDEXING));
  }

  // Toolbar menu: overlay the toolbar / panel on top of the freshly rendered page.
  if (overlay != Overlay::None && usesToolbarMenu()) {
    // The page just re-rendered under the overlay: refresh the snapshot that
    // backs panel->toolbar restores (any previous copy is stale).
    overlayPageStored = renderer.storeBwBuffer();
    renderOverlay();
    // An open option picker rides on top of the freshly drawn panel.
    if (overlayPopup.isActive()) overlayPopup.render(renderer);
    // FAST, same as openOverlay: HALF's inverting pass flashes the sheet
    // (white, in night mode) on every repaint under an open panel. Any AA
    // residue a FAST differential leaves under the chrome has not shown in
    // practice; restore a HALF cleanup here if text ever visibly ghosts
    // through the sheet (see #2190 for the mechanism).
    pushOverlayRefresh();
  }
  pageFrameUsb = gpio.isUsbConnected();
  pageFrameShown = true;
}

void EpubReaderActivity::onEndOfBookRendered() {
  automaticPageTurnActive = false;
  if (pendingSyncSaveError) {
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  }
}

bool EpubReaderActivity::applyDeferredReposition() {
  takePendingDeferredClear();
  if ((!cachedVisibleTextOffset.has_value() && cachedChapterTotalPageCount == 0) || !section || section->isBuilding()) {
    return false;
  }
  bool changed = false;
  if (currentSpineIndex == cachedSpineIndex) {
    int newPage = section->currentPage;
    bool mappedOffset = false;
    if (cachedVisibleTextOffset.has_value()) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*cachedVisibleTextOffset)) {
        newPage = *offsetPage;
        mappedOffset = true;
      }
    }
    if (!mappedOffset && cachedChapterTotalPageCount > 0 && section->pageCount != cachedChapterTotalPageCount) {
      const float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
      newPage = static_cast<int>(progress * static_cast<float>(section->pageCount));
    }
    if (newPage < 0) newPage = 0;
    if (section->pageCount > 0 && newPage >= static_cast<int>(section->pageCount)) {
      newPage = section->pageCount - 1;
    }
    if (newPage != section->currentPage) {
      section->currentPage = newPage;
      changed = true;
    }
  }
  clearDeferredReposition();
  return changed;
}

void EpubReaderActivity::clearDeferredReposition() {
  deferredClearPending.store(false, std::memory_order_release);
  cachedChapterTotalPageCount = 0;
  cachedVisibleTextOffset.reset();
}

void EpubReaderActivity::takePendingDeferredClear() {
  if (deferredClearPending.exchange(false, std::memory_order_acq_rel)) {
    cachedChapterTotalPageCount = 0;
    cachedVisibleTextOffset.reset();
  }
}

const char* EpubReaderActivity::pageBeingLeft() const {
  if (leaving.load(std::memory_order_acquire)) return "leaving";
  if (jumpWaiting.load(std::memory_order_acquire)) return "jump";
  return nullptr;
}

const char* EpubReaderActivity::nextScreenWaiting() const {
  if (const char* reason = pageBeingLeft()) return reason;
  if (pendingManualTurn != 0 || pendingExternalTurn != 0) return "queued";
  return nullptr;
}

// Caller owns RenderLock.
// The owed flag (progressSaveDeferred) clears only once the position is on the card. With no
// section (a chapter jump waiting for its paint) the paint that loads it writes; a card error leaves
// it owed for the exit, and the idle pass does not retry it again until a paint succeeds.
void EpubReaderActivity::saveProgressIfMoved() {
  if (!section) return;
  if (currentSpineIndex != lastSavedSpineIndex || section->currentPage != lastSavedPage ||
      section->pageCount != lastSavedPageCount) {
    if (!saveProgress(currentSpineIndex, section->currentPage, section->estimatedTotalPages())) {
      progressSaveFailed = true;
      return;
    }
    lastSavedSpineIndex = currentSpineIndex;
    lastSavedPage = section->currentPage;
    lastSavedPageCount = section->estimatedTotalPages();
  }
  progressSaveFailed = false;
  progressSaveDeferred.store(false, std::memory_order_relaxed);
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  if (preview) return true;
  std::optional<uint32_t> offset;
  if (section && spineIndex == currentSpineIndex && currentPage >= 0 && currentPage < section->pageCount) {
    offset = (currentPage == section->currentPage && currentPageVisibleOffset.has_value())
                 ? currentPageVisibleOffset
                 : section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage));
  }
  return EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount, offset);
}

void EpubReaderActivity::rememberCurrentContentOffset() {
  cachedVisibleTextOffset.reset();
  if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
    cachedVisibleTextOffset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(section->currentPage));
  }
}

// The quote selector inverts a selection by filling one band per line and redrawing the
// glyphs white; repeating that here is what makes a saved quote look the same as the
// selection did. Word widths come from the advance table the page render just warmed, so
// no glyph is read from the card for this.
void EpubReaderActivity::drawQuoteHighlights(const Page& page, const int fontId, const int marginLeft,
                                             const int marginTop) const {
  if (!quotes::anyAnchorAtOrAfter(quoteAnchors, currentSpineIndex, page.visibleTextOffset)) return;
  std::vector<quotes::PageWord> words;
  quotes::pageWords(page, marginLeft, marginTop, renderer.getFontAscenderSize(fontId), words);
  if (words.empty()) return;
  const int lineHeight = renderer.getLineHeight(fontId);
  std::vector<quotes::WordBox> boxes;
  std::vector<quotes::HighlightBand> bands;
  for (const auto& anchor : quoteAnchors) {
    size_t first = 0, last = 0;
    if (!quotes::coveredWords(anchor, currentSpineIndex, words, first, last)) continue;
    boxes.clear();
    for (size_t i = first; i <= last; i++) {
      const auto& word = words[i];
      boxes.push_back(quotes::WordBox{word.x, word.y,
                                      static_cast<int16_t>(renderer.getTextAdvanceX(fontId, word.text, word.style))});
    }
    quotes::highlightBands(boxes, lineHeight, bands);
    for (const auto& band : bands) renderer.fillRect(band.x, band.y, band.width, band.height);
    for (size_t i = first; i <= last; i++) {
      const auto& word = words[i];
      renderer.drawText(fontId, word.x, word.y, word.text, false, word.style);
    }
  }
}

namespace {
// The chapter's own heading when the tenor/ugly shell writes it by hand: the first page of a chapter
// that opens with the heading the table of contents names, how many of its lines the heading takes
// and the band it stands in. lines is 0 when the page is drawn as laid out. Nothing here touches the
// layout, so the pages, the saved position and the section cache are the same in both shells.
struct HandHeading {
  size_t lines = 0;
  int top = 0, bottom = 0;
  std::string title;
};

HandHeading handHeading(const GfxRenderer& renderer, const Epub& epub, const int spine, const Page& page,
                        const int fontId, const int marginTop) {
  constexpr int MAX_HEADING_LINES = 4;
  HandHeading heading;
  const int toc = epub.getTocIndexForSpineIndex(spine);
  if (toc < 0 || page.hasImages()) return heading;
  heading.title = epub.getTocItem(toc).title;
  std::string lines[MAX_HEADING_LINES];
  int count = 0;
  for (const auto& element : page.elements) {
    if (count == MAX_HEADING_LINES || element->getTag() != TAG_PageLine) break;
    const TextBlock* block = static_cast<const PageLine&>(*element).getBlock();
    for (uint16_t i = 0; block && i < block->wordCount(); ++i) {
      if (i) lines[count] += ' ';
      lines[count] += block->wordText(i);
    }
    ++count;
  }
  const int taken = ugly::logic::headingLines(lines, count, ugly::logic::foldTitle(heading.title));
  if (taken == 0) return heading;
  heading.top = marginTop + page.elements.front()->yPos;
  heading.bottom = static_cast<size_t>(taken) < page.elements.size()
                       ? marginTop + page.elements[taken]->yPos
                       : heading.top + taken * renderer.getLineHeight(fontId);
  heading.lines = taken;
  return heading;
}
}  // namespace

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();
  const int fontId = SETTINGS.getReaderFontId();
  ImageBlock::clearRenderFailures();

  struct PxcSlotGuard {
    ~PxcSlotGuard() { ImageBlock::releaseRenderCache(); }
  } pxcSlotGuard;

  // The page body. With the hand heading of the tenor/ugly shell the heading's own lines are left out
  // and, on the black and white pass only (the gray passes would turn the strokes gray), the chapter
  // title is written in their place. A title that does not fit the band leaves the heading as laid out.
  HandHeading heading;
  // A preview has no section yet: it stands for the first page when the page it replaces was.
  const bool firstPage = section ? section->currentPage == 0 : xemTruoc && nextPageNumber == 0;
  if (shell::isUgly() && !preview && firstPage) {
    heading = handHeading(renderer, *epub, currentSpineIndex, *page, fontId, orientedMarginTop);
    if (heading.lines && !ugly::chapterTitle(renderer, heading.title.c_str(), orientedMarginLeft,
                                             renderer.getScreenWidth() - orientedMarginRight, heading.top,
                                             heading.bottom, false))
      heading.lines = 0;
    LOG_DBG("ERS", "Hand heading lines=%u band=%d-%d", static_cast<unsigned>(heading.lines), heading.top,
            heading.bottom);
  }
  const auto renderPageBody = [&](const bool withTitle) {
    if (heading.lines == 0) {
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
      return;
    }
    for (size_t i = heading.lines; i < page->elements.size(); ++i)
      page->elements[i]->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    if (withTitle)
      ugly::chapterTitle(renderer, heading.title.c_str(), orientedMarginLeft,
                         renderer.getScreenWidth() - orientedMarginRight, heading.top, heading.bottom, true);
  };

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderPageBody(false);
  // Scan the status bar too: a CJK book/chapter title redirected to the SD
  // fallback font joins the page's single batch prewarm instead of triggering
  // its own SD pass after the scope ends.
  renderStatusBar();
  scope.endScanAndPrewarm();
  const auto tPrewarm = millis();
#ifdef TENOR_TURN_TRACE
  tracePaint("PREWARM_END", "page_and_status");
#endif

  const bool pageHasImages = page->hasImages();
  const bool pageHasImagesNeedingDecode = pageHasImages && page->hasImagesNeedingDecode();
  const bool manualRefreshPending = forcedRefreshPending;
  forcedRefreshPending = false;
  const bool cleanImageBasePending = manualRefreshPending || pagesUntilFullRefresh <= 1;
  const bool needsTextGrayscale = SETTINGS.textAntiAliasing;
  const bool needsAnyGrayscale = needsTextGrayscale || pageHasImages;
  bool grayscaleCompleted = !needsAnyGrayscale;
  const bool absoluteImageGrayscale = pageHasImages && !gpio.deviceIsX3() &&
                                      display.getController() == HalDisplay::Controller::UC8279 &&
                                      renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  const auto grayscale = renderer.grayscaleCapabilities(absoluteImageGrayscale ? HalDisplay::GrayscaleMode::Absolute
                                                                               : HalDisplay::GrayscaleMode::Overlay);
  const bool tiledGrayscale = needsAnyGrayscale && grayscale.stripUploads;
  // Paper Mono only (no other panel combines): defer the B/W base activation so
  // the gray planes join it in a single waveform. Displaying the base
  // separately makes the gray pass re-drive the whole text body - a visible
  // flash on every AA page.
  const bool combinedGrayscaleBase =
      tiledGrayscale && !pageHasImages && grayscale.base == HalDisplay::GrayscaleBase::Combined;
  const bool overlapRefresh = tiledGrayscale && grayscale.asyncBase && !pageHasImages;
  // A fast refresh that changes only the status bar (repaintStatusBarAlone) drives only the pixels
  // whose black or white value changes, and every gray pixel of a text page is black in that frame.
  // The UC8279's fast bank leaves unchanged black pixels undriven, so the grays stay; the UC8253's
  // drives them black for two frames, so there only a page without grays stays as painted. An image
  // page's grays are not all black in that frame; its status bar waits for the next paint.
  pageFrameKeepsUnderFast = gpio.deviceIsX3() && !pageHasImages &&
                            (!needsTextGrayscale || display.getController() == HalDisplay::Controller::UC8279);
  auto renderGrayscalePass = [&]() {
    if (absoluteImageGrayscale || needsTextGrayscale) {
      renderPageBody(false);
    } else {
      page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    }
    if (absoluteImageGrayscale) renderStatusBar();
  };

  if (pageHasImagesNeedingDecode) {
    page->renderWithImagePlaceholders(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    renderStatusBar();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
#ifdef TENOR_TURN_TRACE
    tracePaint("PLACEHOLDER_DONE", "image_decode");
#endif
    renderer.clearScreen();
  }

  renderPageBody(true);
  drawQuoteHighlights(*page, fontId, orientedMarginLeft, orientedMarginTop);
  renderStatusBar();
  const auto tBwRender = millis();
#ifdef TENOR_TURN_TRACE
  tracePaint("BW_RENDER_END", pageHasImages ? "image" : "text");
#endif
  // Nothing has reached the panel yet. A press (or Back, or a chapter jump) that arrived while this
  // page was laid out and drawn replaces it, so the next paint goes straight to where the presses
  // lead: one refresh instead of this page's ~1.2 s and then the next. Image pages already showed
  // their placeholder and keep their paint.
  if (!pageHasImages) {
    if (const char* reason = nextScreenWaiting()) {
      LOG_DBG("ERS", "Paint dropped before display: %s", reason);
#ifdef TENOR_TURN_TRACE
      tracePaint("ABORT", reason);
#endif
      // A manual full refresh asked for this page is owed to the page that replaces it.
      forcedRefreshPending = manualRefreshPending;
      paintDropped.store(true, std::memory_order_release);
      return;
    }
  }

  // A text page under an open toolbar sheet stays black and white in the framebuffer: renderBook() draws
  // the sheet over it and pushes both in one fast refresh, and the gray pass runs once, when the sheet
  // closes. Pushing the page first made the page show without the sheet, then the sheet again.
  if (!pageHasImages && overlay != Overlay::None && usesToolbarMenu()) {
    forcedRefreshPending = manualRefreshPending;
    bwUnderSheet = true;
    LOG_DBG("ERS", "Page under the sheet: B/W only, the sheet pushes it");
    return;
  }

  if (absoluteImageGrayscale) {
    const auto baseMode = cleanImageBasePending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, baseMode)) {
      LOG_ERR("ERS", "Could not start absolute image page; displaying B/W");
      ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
#ifdef TENOR_TURN_TRACE
      traceReadablePaint("absolute_bw_fallback");
#endif
      return;
    }
#ifdef TENOR_TURN_TRACE
    traceReadablePaint("absolute_bw_base");
#endif
    LOG_DBG("ERS", "UC8279 image page: absolute quality waveform");
    pagesUntilFullRefresh = 1;
  } else if (pageHasImages) {
    // Image pages use one base refresh before the grayscale pass. FAST leaves
    // the panel receptive to the gray waveform; pending cleanup still honors
    // the scheduled/manual HALF refresh.
    renderer.displayBuffer(cleanImageBasePending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
#ifdef TENOR_TURN_TRACE
    traceReadablePaint("image_bw_base");
#endif
    pagesUntilFullRefresh = 1;
  } else if (combinedGrayscaleBase) {
    // Stash the base without activating; displayGrayBuffer() below commits
    // base + grays as one waveform.
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
#ifdef TENOR_TURN_TRACE
    tracePaint("BASE_STAGED", "combined");
#endif
  } else if (needsAnyGrayscale) {
    if (pagesUntilFullRefresh <= 1) {
      // A cleanup refresh settles X3 correctly only when its grayscale
      // preconditioning waveform runs before the gray planes are written.
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
#ifdef TENOR_TURN_TRACE
      traceReadablePaint("half_refresh");
#endif
      renderer.preconditionGrayscale();
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else if (overlapRefresh) {
      ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, /*async=*/true);
#ifdef TENOR_TURN_TRACE
      tracePaint("BASE_SUBMITTED", "async");
#endif
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
#ifdef TENOR_TURN_TRACE
      traceReadablePaint("gray_bw_base");
#endif
      pagesUntilFullRefresh--;
    }
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
#ifdef TENOR_TURN_TRACE
    traceReadablePaint("bw");
#endif
  }
  const auto tDisplay = millis();

  if (tiledGrayscale) {
    constexpr int STRIP_ROWS = 80;
    const int gh = renderer.getDisplayHeight();
    const int gwBytes = renderer.getDisplayWidthBytes();
    const size_t planeBytes = static_cast<size_t>(gwBytes) * gh;

    auto renderPlaneToBuffer = [&](const bool lsbPlane, uint8_t* buf) {
      renderer.setRenderMode(lsbPlane ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
      for (int y = 0; y < gh; y += STRIP_ROWS) {
        const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
        renderer.beginStripTarget(buf + static_cast<size_t>(y) * gwBytes, y, rows);
        renderer.clearScreen(0x00);
        renderGrayscalePass();
        renderer.endStripTarget();
      }
    };

    constexpr size_t PLANE_BUF_HEADROOM = 60000;
    constexpr size_t PLANE_BUF_MAX_ALLOC_RESERVE = 16 * 1024;
    const auto planeBufFits = [planeBytes] {
      return ESP.getFreeHeap() >= planeBytes + PLANE_BUF_HEADROOM &&
             ESP.getMaxAllocHeap() >= planeBytes + PLANE_BUF_MAX_ALLOC_RESERVE;
    };
    auto lsbPlaneBuf = (overlapRefresh && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;
    auto msbPlaneBuf = (lsbPlaneBuf && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;

    if (lsbPlaneBuf) {
      renderPlaneToBuffer(true, lsbPlaneBuf.get());
      if (msbPlaneBuf) renderPlaneToBuffer(false, msbPlaneBuf.get());
      const auto tGrayRender = millis();
#ifdef TENOR_TURN_TRACE
      tracePaint("GRAY_RENDER_END", msbPlaneBuf ? "two_planes" : "one_plane");
#endif

      renderer.waitRefreshComplete();
      const auto tWait = millis();
#ifdef TENOR_TURN_TRACE
      traceReadablePaint("async_wait");
#endif

      renderer.writeGrayscalePlaneStrip(true, lsbPlaneBuf.get(), 0, gh);
      if (msbPlaneBuf) {
        renderer.writeGrayscalePlaneStrip(false, msbPlaneBuf.get(), 0, gh);
      } else {
        renderPlaneToBuffer(false, lsbPlaneBuf.get());
        renderer.writeGrayscalePlaneStrip(false, lsbPlaneBuf.get(), 0, gh);
      }
      const auto tGrayWrite = millis();

      renderer.setRenderMode(GfxRenderer::BW);
      renderer.displayGrayBuffer();
      grayscaleCompleted = true;
      const auto tGrayDisplay = millis();
#ifdef TENOR_TURN_TRACE
      tracePaint("GRAY_DONE", "buffered");
      traceReadablePaint("buffered_gray");
#endif

      renderer.cleanupGrayscaleWithFrameBuffer();
      const auto tEnd = millis();
#ifdef TENOR_TURN_TRACE
      tracePaint("CLEANUP_END", "buffered");
#endif

      LOG_DBG("ERS",
              "Page render (tiled async): prewarm=%lums bw_render=%lums display=%lums gray_render=%lums "
              "wait=%lums gray_write=%lums gray_display=%lums cleanup=%lums total=%lums (planes buffered: %d)",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayRender - tDisplay, tWait - tGrayRender,
              tGrayWrite - tWait, tGrayDisplay - tGrayWrite, tEnd - tGrayDisplay, tEnd - t0, msbPlaneBuf ? 2 : 1);
    } else {
      auto scratch = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(gwBytes) * STRIP_ROWS);
      renderer.waitRefreshComplete();
#ifdef TENOR_TURN_TRACE
      if (!combinedGrayscaleBase && !absoluteImageGrayscale) traceReadablePaint("strip_wait");
#endif
      // The page is readable now. When the reader is leaving it (Back, a held chapter jump), the
      // ~500 ms of strips, gray waveform and cleanup below would only hold the next screen back.
      const char* grayNotRun = !scratch ? "scratch_oom" : pageBeingLeft();
      if (grayNotRun) {
        if (!scratch) {
          LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping AA this page", gwBytes * STRIP_ROWS);
        } else {
          LOG_DBG("ERS", "Gray pass skipped: %s", grayNotRun);
        }
#ifdef TENOR_TURN_TRACE
        tracePaint("GRAY_SKIPPED", grayNotRun);
#endif
        if (overlapRefresh || combinedGrayscaleBase) {
          // The BW refresh ran the shadow-free async path, so controller RAM's
          // differential baseline was never rebuilt. Even with AA skipped it must
          // be re-synced from the intact BW framebuffer, or the next differential
          // update diffs against stale contents. On the combined-base path the
          // base activation is still deferred; this cleanup commits it so the
          // page reaches the panel even without its grays.
          renderer.cleanupGrayscaleWithFrameBuffer();
#ifdef TENOR_TURN_TRACE
          tracePaint("CLEANUP_END", grayNotRun);
          traceReadablePaint("cleanup_bw");
#endif
        }
      } else {
        // A turn or an exit that arrives while the strips are drawn stops them too. The planes then
        // hold part of a gray pass, so instead of showing it the cleanup rewrites both from the page.
        const char* stoppedBy = nullptr;
        const auto drawPlane = [&](const bool lsbPlane) {
          renderer.setRenderMode(lsbPlane ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
          for (int y = 0; y < gh; y += STRIP_ROWS) {
            if ((stoppedBy = pageBeingLeft())) return;
            const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
            renderer.beginStripTarget(scratch.get(), y, rows);
            renderer.clearScreen(0x00);
            renderGrayscalePass();
            renderer.endStripTarget();
            renderer.writeGrayscalePlaneStrip(lsbPlane, scratch.get(), y, rows);
          }
        };
        drawPlane(true);
        const auto tGrayLsb = millis();
        if (!stoppedBy) drawPlane(false);
        const auto tGrayMsb = millis();

        renderer.setRenderMode(GfxRenderer::BW);
        if (stoppedBy) {
          LOG_DBG("ERS", "Gray pass skipped: %s", stoppedBy);
#ifdef TENOR_TURN_TRACE
          tracePaint("GRAY_SKIPPED", stoppedBy);
#endif
          renderer.cleanupGrayscaleWithFrameBuffer();
#ifdef TENOR_TURN_TRACE
          tracePaint("CLEANUP_END", stoppedBy);
#endif
          return;
        }
        renderer.displayGrayBuffer();
        grayscaleCompleted = true;
        const auto tGrayDisplay = millis();
#ifdef TENOR_TURN_TRACE
        tracePaint("GRAY_DONE", "strip");
        traceReadablePaint("strip_gray");
#endif

        renderer.cleanupGrayscaleWithFrameBuffer();
        const auto tCleanup = millis();
#ifdef TENOR_TURN_TRACE
        tracePaint("CLEANUP_END", "strip");
#endif

        const auto tEnd = millis();
        LOG_DBG("ERS",
                "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums gray_lsb=%lums "
                "gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
                tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayLsb - tDisplay, tGrayMsb - tGrayLsb,
                tGrayDisplay - tGrayMsb, tCleanup - tGrayDisplay, tEnd - t0);
      }
    }
  } else {
    if (needsAnyGrayscale) {
      if (!renderer.storeBwBuffer()) {
        LOG_ERR("ERS", "Failed to store BW buffer for grayscale render; skipping grayscale this page");
#ifdef TENOR_TURN_TRACE
        tracePaint("GRAY_SKIPPED", "bw_store_oom");
#endif
        if (absoluteImageGrayscale) renderer.setRenderMode(GfxRenderer::BW);
        return;
      }
      const auto tBwStore = millis();

      renderer.clearScreen(absoluteImageGrayscale ? 0xFF : 0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      renderGrayscalePass();
      renderer.copyGrayscaleLsbBuffers();
      const auto tGrayLsb = millis();

      renderer.clearScreen(absoluteImageGrayscale ? 0xFF : 0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      renderGrayscalePass();
      renderer.copyGrayscaleMsbBuffers();
      const auto tGrayMsb = millis();

      renderer.displayGrayBuffer();
      grayscaleCompleted = true;
      const auto tGrayDisplay = millis();
#ifdef TENOR_TURN_TRACE
      tracePaint("GRAY_DONE", "full_planes");
      traceReadablePaint("full_gray");
#endif
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.restoreBwBuffer();
      const auto tBwRestore = millis();
#ifdef TENOR_TURN_TRACE
      tracePaint("CLEANUP_END", "bw_restore");
#endif

      const auto tEnd = millis();
      LOG_DBG("ERS",
              "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
              "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, tGrayLsb - tBwStore,
              tGrayMsb - tGrayLsb, tGrayDisplay - tGrayMsb, tBwRestore - tGrayDisplay, tEnd - t0);
    } else {
      const auto tEnd = millis();
      LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", tPrewarm - t0,
              tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - t0);
    }
  }
  if (grayscaleCompleted) markClosedTextFrameUpLocked();
}

// Caller owns RenderLock, on the main loop: the battery reads the fuel gauge, which only that task
// may read. The status bar is drawn again over the page frame still in the framebuffer and sent
// with one fast refresh; the page is not laid out, drawn or given a gray pass again.
void EpubReaderActivity::repaintStatusBarAlone() {
  if (!pageFrameShown || preview || SETTINGS.readerStatusBarHidden()) return;
  const auto sb = SETTINGS.statusBarSpec();
  const bool usb = gpio.isUsbConnected();
  const bool usbChanged = sb.showBattery && usb != pageFrameUsb;
  const bool noteChanged = sb.showsTitle() && bleturner::linkNote() != linkNoteDrawn;
  if (!usbChanged && !noteChanged) return;
  if (!pageFrameKeepsUnderFast || !tenorchrome::enabled()) {
    // This page does not keep under a fast refresh of its status bar alone. The charging mark
    // waits for the next paint; the link note is painted with the page now.
    if (noteChanged) requestUpdate();
    return;
  }
#ifdef TENOR_PRESS_PROBE
  const unsigned long started = millis();
#endif
  const int top = tenorchrome::readerStatusTop(renderer.getScreenHeight());
  renderer.fillRect(0, top, renderer.getScreenWidth(), renderer.getScreenHeight() - top, false);
  auto scope = renderer.getFontCacheManager()->createPrewarmScope();
  renderStatusBar();
  scope.endScanAndPrewarm();
  renderStatusBar();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  pageFrameUsb = usb;
  LOG_DBG("ERS", "Status bar repainted alone: usb=%d", usb ? 1 : 0);
#ifdef TENOR_PRESS_PROBE
  LOG_INF("ERS", "STATUS_REPAINT usb=%d note=%d ms=%lu", usb ? 1 : 0, static_cast<int>(linkNoteDrawn),
          millis() - started);
#endif
}

void EpubReaderActivity::renderStatusBar() const {
  if (preview) {
    drawPreviewFooter();
    return;
  }
  // A preview keeps the numbers of the layout it replaces until the chapter is laid out again.
  const int currentPage = section ? section->currentPage + 1 : xemTruoc ? std::max(0, nextPageNumber) + 1 : 1;
  const float pageCount = section    ? section->estimatedTotalPages()
                          : xemTruoc ? std::max(currentPage, cachedChapterTotalPageCount)
                                     : 1;
  const float sectionChapterProg = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) : 0;
  // Unknown (-1, not drawn) while the book still builds its chapter sizes.
  const float bookProgress = !epub                   ? 0
                             : !epub->indexComplete() ? -1
                                                      : epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100;

  std::string title;
  int textYOffset = 0;
  const auto sb = SETTINGS.statusBarSpec();

  const bool linkNote = sb.showsTitle() && linkNoteTitle(title);
  if (linkNote) {
    // The page turner's link note stands in for the title, the automatic turn's line included.
  } else if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(60 * 1000 / pageTurnDuration);
    const uint8_t statusBarHeight = readerStatusBarHeight();
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE && epub && !epub->indexComplete()) {
    // No TOC yet: the book's title stands in for the chapter's.
    title = epub->getTitle();
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    if (epub) {
      const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
      if (tocIndex != -1) {
        const auto tocItem = epub->getTocItem(tocIndex);
        title = tocItem.title;
      }
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub ? epub->getTitle() : "";
  }

  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset, true, currentPageBookmarked,
                    section ? section->isBuilding() : false, !linkNote);
}

// ---------------------------------------------------------------------------
// Toolbar reader menu
// ---------------------------------------------------------------------------

namespace {
constexpr StrId kTextRowNames[] = {StrId::STR_FONT, StrId::STR_FONT_SIZE, StrId::STR_LINE_SPACING,
                                   StrId::STR_PARA_ALIGNMENT, StrId::STR_FOCUS_READING};
constexpr StrId kSpacingIds[] = {StrId::STR_INK_DEFAULT, StrId::STR_VERY_NARROW, StrId::STR_TIGHT, StrId::STR_WIDE,
                                 StrId::STR_VERY_WIDE};
// Tắt / Mặc định / Lớn, in the same order as readerSpacing::DropCapMode.
constexpr StrId kDropCapIds[] = {StrId::STR_STATE_OFF, StrId::STR_INK_DEFAULT, StrId::STR_SPACING_LARGE};
constexpr StrId kAlignIds[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                               StrId::STR_BOOK_S_STYLE};
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
// X4 Pro (founder 06/10): all 14 text settings, in the order of Settings > Reader > Text settings. A row's id
// is its place in readermenu::TEXT_KEYS (the key a Favorites pin stores): ids 0-4 are the rows above, ids 5 and
// on the other text settings of the catalog, read and stepped as Settings does.
constexpr uint8_t kTextRowIds[] = {0, 1, 2, 5, 6, 7, 3, 8, 9, 10, 4, 11, 12, 13};
static_assert(std::size(kTextRowIds) == readermenu::TEXT_KEY_COUNT, "every text row has a place");
#else
constexpr uint8_t kTextRowIds[] = {0, 1, 2, 3, 4};
#endif
constexpr int kTextRowCount = static_cast<int>(std::size(kTextRowIds));
int textRowId(const int row) { return row >= 0 && row < kTextRowCount ? kTextRowIds[row] : -1; }
// Contents, Text, More and Favorites on every board (founder 06/10).
constexpr int kReaderTools = ReaderToolbarUi::kToolCount;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
static_assert(kReaderTools == tenorchrome::READER_TOOLS, "the touch bar draws the same tools");
#endif
// The place on the Text panel of the row with this id, or -1.
int textRowPlace(const int id) {
  for (int row = 0; row < kTextRowCount; ++row)
    if (kTextRowIds[row] == id) return row;
  return -1;
}
// A row of the catalog (ids 5 and on), or nullptr.
const SettingInfo* catalogTextRow(const int id) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (id < 5 || id >= readermenu::TEXT_KEY_COUNT) return nullptr;
  for (const auto& info : getBaseSettingsList())
    if (info.key && strcmp(info.key, readermenu::TEXT_KEYS[id]) == 0) return &info;
#endif
  (void)id;
  return nullptr;
}
static_assert(std::size(kSpacingIds) == readerSpacing::LEVEL_COUNT, "line spacing labels");
static_assert(std::size(kAlignIds) == CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, "alignment labels");
static_assert(std::size(kDropCapIds) == readerSpacing::DROP_CAP_MODE_COUNT, "drop cap labels");
// The Text rows of several values, each value at its place in the order shown: line spacing tightest
// first (so its places are the levels in that order), alignment and drop cap (places = the stored values).
constexpr uint8_t kSpacingByPlace[] = {1, 2, 0, 3, 4};
static_assert(std::size(kSpacingByPlace) == readerSpacing::LEVEL_COUNT, "line spacing places");
int textChoiceCount(const int row) {
  return row == 2 ? readerSpacing::LEVEL_COUNT
         : row == 3 ? CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT
         : row == 4 ? readerSpacing::DROP_CAP_MODE_COUNT
                    : 0;
}
int textChoiceInUse(const int row) {
  if (row == 2) {
    const uint8_t level = readerSpacing::clampLevel(SETTINGS.lineSpacing);
    for (int k = 0; k < static_cast<int>(std::size(kSpacingByPlace)); ++k)
      if (kSpacingByPlace[k] == level) return k;
    return -1;
  }
  if (row == 3) return SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT;
  if (row == 4) return readerSpacing::clampDropCapMode(SETTINGS.dropCapMode);
  return -1;
}
// The name of the value at `place` on a row of values, in the order they are shown.
StrId textChoiceLabel(const int row, const int place) {
  return row == 2 ? kSpacingIds[kSpacingByPlace[place]] : row == 3 ? kAlignIds[place] : kDropCapIds[place];
}
}  // namespace

// A value tapped on its row: straight to it, whatever was in use (the page is the preview).
void EpubReaderActivity::chooseTextValue(const int row, const int place) {
  {
    RenderLock lock;
    if (place < 0 || place >= textChoiceCount(row) || place == textChoiceInUse(row)) return;
    if (row == 2) SETTINGS.lineSpacing = kSpacingByPlace[place];
    if (row == 3) SETTINGS.paragraphAlignment = static_cast<uint8_t>(place);
    if (row == 4) SETTINGS.dropCapMode = static_cast<uint8_t>(place);
    invalidateTextSettingsLocked();
  }
  applyTextSettingLive();
}

#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
void EpubReaderActivity::openPick(const int source, std::string title) {
  Pick next;
  next.source = source;
  next.origin = panelIndex;
  next.title = std::move(title);
  if (source == 1) {
    // The point sizes the active family actually ships.
    const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
    const uint8_t cur = sizes.empty() ? 0 : snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
    for (size_t i = 0; i < sizes.size(); ++i) {
      next.labels.push_back(std::to_string(sizes[i]) + " pt");
      if (sizes[i] == cur) next.inUse = static_cast<int>(i);
    }
  } else if (source >= 2 && source <= 4) {
    for (int place = 0; place < textChoiceCount(source); ++place)
      next.labels.emplace_back(I18N.get(textChoiceLabel(source, place)));
    next.inUse = textChoiceInUse(source);
  } else if (source == PICK_MORE + static_cast<int>(readermenu::Action::STATUS_BAR)) {
    for (const auto id : readermenu::STATUS_BAR_MODE_LABELS) next.labels.emplace_back(I18N.get(id));
    next.inUse = SETTINGS.readerStatusBarMode;
  }
  if (next.labels.empty()) return;
  RenderLock lock;  // the render task reads the list
  next.sheetRows = toolbarUi->visibleRows();
  pick = std::move(next);
  textDepth = TextDepth::Pick;
  panelIndex = std::max(0, pick.inUse);
  toolbarUi->nav().reset(panelIndex);
  redrawSheetLocked();
}

void EpubReaderActivity::leavePick(const bool keep) {
  const int source = pick.source;
  const int place = panelIndex;
  const bool changed = keep && place != pick.inUse;
  {
    RenderLock lock;
    textDepth = TextDepth::Rows;
    panelIndex = pick.origin;
    toolbarUi->nav().reset(panelIndex);
    pick = Pick{};
    if (!changed) {
      redrawSheetLocked();
      return;
    }
  }
  if (!applyPick(source, place)) {
    RenderLock lock;
    redrawSheetLocked();
  }
}

bool EpubReaderActivity::applyPick(const int source, const int place) {
  if (source == 1) {
    const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
    if (place < 0 || place >= static_cast<int>(sizes.size())) return false;
    {
      RenderLock lock;
      SETTINGS.fontPointSize = sizes[place];
      applyReaderTextSettingsLocked();
    }
    applyTextSettingLive();
    return true;
  }
  if (source >= 2 && source <= 4) {
    chooseTextValue(source, place);
    return true;
  }
  if (source == PICK_MORE + static_cast<int>(readermenu::Action::STATUS_BAR)) {
    setReaderStatusBarMode(place);
    return true;
  }
  return false;
}
#endif

static_assert(std::size(readermenu::STATUS_BAR_MODE_LABELS) == CrossPointSettings::READER_STATUS_BAR_MODE_COUNT,
              "a name a status bar mode");

// The reader status bar's mode chosen in the menu. Its height can change, so the page is laid out again;
// the mode reaches the card with the text settings, once the page is back.
void EpubReaderActivity::setReaderStatusBarMode(const int mode) {
  {
    RenderLock lock;
    if (mode < 0 || mode >= CrossPointSettings::READER_STATUS_BAR_MODE_COUNT || mode == SETTINGS.readerStatusBarMode)
      return;
    SETTINGS.readerStatusBarMode = static_cast<uint8_t>(mode);
    invalidateTextSettingsLocked();
  }
  applyTextSettingLive();
}

bool EpubReaderActivity::readingPageVisible() const { return section && overlay == Overlay::None && !isAtEndOfBook(); }

bool EpubReaderActivity::usesToolbarMenu() const {
  // Both board classes drive the same chrome: touch through the FreeInkUI tap
  // targets, buttons through the focused-tool pill and the panel cursor.
  // The X4 Pro opens the touch toolbar, including when an earlier firmware saved another menu style.
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  return true;
#else
  return !tenorchrome::kTouchShell && SETTINGS.readerMenuStyle == CrossPointSettings::READER_MENU_TOOLBAR;
#endif
}

std::string EpubReaderActivity::currentChapterTitle() const {
  if (!epub) return "";
  const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex != -1) {
    return epub->getTocItem(tocIndex).title;
  }
  return tr(STR_UNNAMED);
}

std::string EpubReaderActivity::textRowName(int row) const {
  row = textRowId(row);
  if (const auto* info = catalogTextRow(row)) return I18N.get(info->nameId);
  return row >= 0 && row < static_cast<int>(std::size(kTextRowNames)) ? I18N.get(kTextRowNames[row]) : "";
}

std::string EpubReaderActivity::textRowValue(int row) const {
  static constexpr StrId kFamily[] = {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS};
  row = textRowId(row);
  if (const auto* info = catalogTextRow(row)) return SettingsActivity::settingValueText(*info);
  switch (row) {
    case 0:  // opens the family list (the chevron is the row's own)
      if (SETTINGS.sdFontFamilyName[0] != '\0') return SETTINGS.sdFontFamilyName;
      return I18N.get(kFamily[SETTINGS.fontFamily % CrossPointSettings::FONT_FAMILY_COUNT]);
    case 1:
      return std::to_string(SETTINGS.fontPointSize)
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
          + " pt"
#endif
          ;
    case 2:
      return I18N.get(kSpacingIds[readerSpacing::clampLevel(SETTINGS.lineSpacing)]);
    case 3:
      return I18N.get(kAlignIds[SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT]);
    case 4:
      return I18N.get(kDropCapIds[readerSpacing::clampDropCapMode(SETTINGS.dropCapMode)]);
    default:
      return "";
  }
}

// Live apply: re-paginate, and let renderBook() redraw the page with
// the open panel back on top -- the book itself is the preview.
void EpubReaderActivity::applyTextSettingLive() {
  requestUpdate();
}

// Settings-style option pickers for the Text panel's enum rows. Every
// selection applies immediately to the page under the sheet.
void EpubReaderActivity::cycleTextRow(int row) {
  row = textRowId(row);
  {
    RenderLock lock;  // the render task must not paint a page laid out with the old value in the new one
    if (const auto* info = catalogTextRow(row)) {
      // A tap steps it, as a tap steps alignment: the next value, the first after the last.
      auto& value = SETTINGS.*(info->valuePtr);
      if (info->type == SettingType::TOGGLE) {
        value = !value;
      } else if (info->type == SettingType::VALUE) {
        const auto range = info->valueRange;
        value = value + range.step > range.max ? range.min : value + range.step;
      } else {
        const int count = static_cast<int>(info->enumLabels().size());
        if (count <= 0) return;
        value = static_cast<uint8_t>((value + 1) % count);
      }
      row = -1;
    }
    switch (row) {
      case -1:  // a catalog row, stepped above
        break;
      case 3:
        SETTINGS.paragraphAlignment =
            static_cast<uint8_t>((SETTINGS.paragraphAlignment + 1) % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
        break;
      case 4:
        SETTINGS.dropCapMode = static_cast<uint8_t>((SETTINGS.dropCapMode + 1) % readerSpacing::DROP_CAP_MODE_COUNT);
        break;
      default:
        return;
    }
    applyReaderTextSettingsLocked();
  }
  applyTextSettingLive();
}

void EpubReaderActivity::showTextRowPopup(const int row) {
  switch (row) {
    case 1: {
      // The point sizes the active family actually ships.
      const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
      if (sizes.empty()) return;
      std::vector<std::string> labels;
      labels.reserve(sizes.size());
      for (const uint8_t size : sizes) labels.push_back(std::to_string(size) + " pt");
      const uint8_t cur = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
      int curIdx = 0;
      for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i] == cur) curIdx = static_cast<int>(i);
      }
      overlayPopup.show(StrId::STR_FONT_SIZE, labels, curIdx, [this, sizes](int idx) {
        if (idx < 0 || idx >= static_cast<int>(sizes.size())) return;
        {
          RenderLock lock;
          SETTINGS.fontPointSize = sizes[idx];
          applyReaderTextSettingsLocked();
        }
        applyTextSettingLive();
      });
      break;
    }
    case 2:
      overlayPopup.show(StrId::STR_LINE_SPACING, kSpacingIds, static_cast<int>(std::size(kSpacingIds)),
                        readerSpacing::clampLevel(SETTINGS.lineSpacing), [this](int idx) {
                          {
                            RenderLock lock;
                            SETTINGS.lineSpacing = static_cast<uint8_t>(idx);
                            invalidateTextSettingsLocked();
                          }
                          applyTextSettingLive();
                        });
      break;
    case 3:
      overlayPopup.show(StrId::STR_PARA_ALIGNMENT, kAlignIds, static_cast<int>(std::size(kAlignIds)),
                        SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, [this](int idx) {
                          {
                            RenderLock lock;
                            SETTINGS.paragraphAlignment = static_cast<uint8_t>(idx);
                            invalidateTextSettingsLocked();
                          }
                          applyTextSettingLive();
                        });
      break;
    default:
      return;
  }
  paintOverlayPopup();
}

void EpubReaderActivity::discardOverlayPage() {
  if (!overlayPageStored) return;
  renderer.discardStoredBwBuffer();
  overlayPageStored = false;
}

// Push freshly painted overlay chrome. Where the panel supports it the refresh
// is fired deferred: the loop keeps polling input while the waveform runs, so
// the chrome answers taps and buttons the moment it is visible instead of only
// after a blocking displayBuffer() returns. Caller must hold the RenderLock.
void EpubReaderActivity::pushOverlayRefresh() {
  if (renderer.supportsAsyncRefresh()) {
    renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
    overlayRefreshPending = true;
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

// The sheet drawn again from the clean page up: what the old sheet covered and the new one does not (a
// shorter sheet, another level) shows the page again. One push. Caller must hold the RenderLock.
void EpubReaderActivity::redrawSheetLocked() {
  settleOverlayRefresh();
  if (overlayPageStored) {
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = renderer.storeBwBuffer();
  }
  renderOverlay();
  pushOverlayRefresh();
}

// Wait out a pending deferred overlay refresh and reseed the panel's
// differential baseline from the framebuffer: the shadow-free async path skips
// the post-refresh resync, so without this the next FAST diff would run
// against the frame from before the chrome and leave stale pixels on the
// glass. Caller must hold the RenderLock.
void EpubReaderActivity::settleOverlayRefresh() {
  if (!overlayRefreshPending) return;
  overlayRefreshPending = false;
  renderer.cleanupGrayscaleWithFrameBuffer();  // waits, then reseeds the baseline
}

void EpubReaderActivity::openOverlay(Overlay target) {
  if (target == Overlay::Contents && waitsForIndex()) return;
  mappedInput.resetHomeButtonInput();
  const Overlay previous = overlay;
  if (previous == Overlay::None) {
    bwUnderSheet = false;
    textCloseFrame.store(0, std::memory_order_relaxed);
  }
  overlay = target;
  textDepth = TextDepth::Rows;
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
  pick = Pick{};
#endif
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  spacingDragging = false;
  pointSizeDraft.clear();
#endif
  fontFamilies.clear();
  if (!toolbarUi) toolbarUi = std::make_unique<ReaderToolbarUi>(renderer);
  if (previous == Overlay::None) toolbarUi->begin();
  // Buttons show a cursor from the start; touch boards only once a button moves it.
  panelCursorShown = !mappedInput.hasTouch();
  switch (target) {
    case Overlay::Toolbar:
      focusedTool = 0;
      break;
    case Overlay::Contents:
      panelIndex = std::max(0, epub->getTocIndexForSpineIndex(currentSpineIndex));
      // Fresh viewport opening on the current chapter, cursor shown or not.
      toolbarUi->nav().reset(panelIndex);
      toolbarUi->nav().top = panelIndex;
      break;
    case Overlay::Text:
      panelIndex = 0;
      toolbarUi->nav().reset();
      break;
    case Overlay::More:
      panelIndex = 0;
      buildMoreActions();
      toolbarUi->nav().reset();
      break;
    case Overlay::Favorites:
      panelIndex = 0;
      toolbarUi->nav().reset();
      break;
    default:
      break;
  }
  if (target != Overlay::Toolbar) loadPins();  // a hold on a Text or More row toggles these; Favorites lists them
  panelHoldJumped = false;

  // The page is already on screen and still in the framebuffer, so paint the
  // chrome straight onto it and push one refresh. requestUpdate() would
  // re-render the whole page first: slow, and visibly wrong, since that repaint
  // lands before the overlay does.
  //
  // Refresh mode: FAST for every overlay paint, first open included. The AA
  // pass only grays glyph edges, and residue a FAST differential leaves under
  // the sheet has not shown in practice; it also self-heals on the
  // Xteink-class panels, whose close path re-renders the page. If text or
  // images ever visibly ghost through the chrome, restore a HALF cleanup on
  // the first open (see #2190 for the mechanism).
  if (section) {
    // Serialize against the render task: renderBook may be mid-page (status
    // bar included) in the shared framebuffer, and painting the chrome from
    // the loop task at the same time interleaves the two frames.
    RenderLock lock;
    settleOverlayRefresh();
    if (previous == Overlay::None) {
      // Snapshot the clean page so stepping back from a panel to the toolbar
      // (and closing, where supported) can restore it without a re-render.
      overlayPageStored = renderer.storeBwBuffer();
    } else if (overlayPageStored) {
      // Overlay -> overlay: wipe the previous chrome (toolbar header, sheet,
      // progress row) back to the clean page so none of it shows around or
      // through the new sheet; re-store for the next transition. No baseline
      // resync: the glass still shows the old chrome, and the differential
      // must keep diffing against it to erase it.
      renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
      overlayPageStored = renderer.storeBwBuffer();
    }
    renderOverlay();
    pushOverlayRefresh();
  } else {
    requestUpdate();  // no page yet: renderBook() draws the overlay once it is
  }
}

// Close the overlay back to the reading page. Boards without the Xteink
// grayscale-AA pass restore the page snapshot and push one FAST refresh -- no
// re-render, no flash; Xteink boards re-render to restore the AA planes.
void EpubReaderActivity::closeOverlayToPage() {
  mappedInput.resetHomeButtonInput();
  bool frameUp = false;
  {
    RenderLock lock;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    tenorchrome::noteReaderFootBar(false, false, -1);
#endif
    overlay = Overlay::None;
    overlayPopup.dismiss();
    toolbarUi.reset();
    settleOverlayRefresh();
    if (!bwUnderSheet && !xteinkClassPanel() && overlayPageStored) {
      renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
      overlayPageStored = false;
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      frameUp = true;
    } else {
      discardOverlayPage();
    }
    panelClosedLocked(/*leaving=*/false, frameUp);
  }
  if (!frameUp) requestUpdate();
}

void EpubReaderActivity::renderOverlay() {
  if (!epub || (!section && !xemTruoc) || !toolbarUi) return;

  ReaderToolbarUi::Model model;
  const auto drawMenuChrome = [this]() {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    if (!SETTINGS.globalStatusBarHidden()) {
      renderer.fillRect(0, 0, renderer.getScreenWidth(), tenorchrome::readerStripBottom(renderer), false);
      tenorchrome::drawStatus(renderer);
    }
#else
    if (!SETTINGS.globalStatusBarHidden()) {
      const auto orientation = renderer.getOrientation();
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      const int footer = UITheme::getInstance().getMetrics().buttonHintsHeight;
      renderer.fillRect(0, renderer.getScreenHeight() - footer, renderer.getScreenWidth(), footer, false);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT),
          overlay == Overlay::Toolbar ? tr(STR_DIR_LEFT) : tr(STR_DIR_UP),
          overlay == Overlay::Toolbar ? tr(STR_DIR_RIGHT) : tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.setOrientation(orientation);
    }
#endif
  };
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
  if (!SETTINGS.globalStatusBarHidden()) {
    const auto footer = static_cast<int16_t>(UITheme::getInstance().getMetrics().buttonHintsHeight);
    switch (renderer.getOrientation()) {
      case GfxRenderer::Orientation::Portrait: model.footerInsets.bottom = footer; break;
      case GfxRenderer::Orientation::LandscapeClockwise: model.footerInsets.left = footer; break;
      case GfxRenderer::Orientation::PortraitInverted: model.footerInsets.top = footer; break;
      case GfxRenderer::Orientation::LandscapeCounterClockwise: model.footerInsets.right = footer; break;
    }
  }
#endif
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  const int chromeTool = overlay == Overlay::Contents ? 0 : overlay == Overlay::Text ? 1 : overlay == Overlay::More ? 2
                         : overlay == Overlay::Favorites                                         ? 3
                                                                                                 : -1;
  tenorchrome::noteReaderFootBar(overlay != Overlay::None,
                               overlay == Overlay::Text && textDepth == TextDepth::PointSize, chromeTool);
#endif
  // The toolbar's tool pill is the button-navigation cursor: tap-first (same
  // convention as the panel lists), it only shows once a button has moved it.
  // Panels override below: there the pill marks the open panel on every board.
  model.activeTool = (overlay == Overlay::Toolbar && !panelCursorShown) ? -1 : focusedTool;
  // Strings the model points at live here until render() returns.
  std::string chapterTitle, pageInfo;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  std::string numericHint;
#endif

  if (overlay == Overlay::Toolbar) {
    chapterTitle = currentChapterTitle();
    // A preview shows the numbers of the layout it replaces (see renderStatusBar).
    const int page = section ? section->currentPage : std::max(0, nextPageNumber);
    const int pageCount = section ? section->estimatedTotalPages() : std::max(page + 1, cachedChapterTotalPageCount);
    const float chapterProgress = pageCount > 0 ? static_cast<float>(page + 1) / static_cast<float>(pageCount) : 0.0f;
    const float bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress);
    pageInfo = std::to_string(page + 1) + "/" + std::to_string(pageCount) + "   " +
               std::to_string(clampPercent(static_cast<int>(bookProgress * 100.0f + 0.5f))) + "%";
    model.chapterTitle = chapterTitle.c_str();
    model.pageInfo = pageInfo.c_str();
    model.progressPermille = static_cast<int>(bookProgress * 1000.0f + 0.5f);
    toolbarUi->setModel(model);
    toolbarUi->render();
    drawMenuChrome();
    return;
  }

  // Panels (Contents / Text / More): a bottom sheet over the page.
  model.panel = true;
  if (!mappedInput.hasTouch()) {
    model.denseRows = true;
  }
  // Tap-first: the cursor is only drawn once a button has moved it, so a
  // tapped row does not stay inverted after its action.
  model.selectedIndex = panelCursorShown ? panelIndex : -1;
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
  if (textDepth == TextDepth::Pick) {
    model.panelTitle = pick.title.c_str();
    model.itemCount = static_cast<int>(pick.labels.size());
    model.sheetRows = pick.sheetRows;
    model.rowText = [this](int i) { return i < static_cast<int>(pick.labels.size()) ? pick.labels[i] : ""; };
    model.rowMarked = [this](int i) { return i == pick.inUse; };
  } else
#endif
  if (overlay == Overlay::Contents) {
    model.panelTitle = tr(STR_TOOL_CONTENTS);
    model.itemCount = epub->getTocItemsCount();
    const int currentTocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    model.rowMarked = [currentTocIndex](int i) { return currentTocIndex >= 0 && i == currentTocIndex; };
    model.rowText = [this](int i) {
      const auto item = epub->getTocItem(i);
      const int depth = item.level > 1 ? (item.level - 1) * 2 : 0;
      return std::string(depth, ' ') + item.title;
    };
  } else if (overlay == Overlay::Text) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    model.textView = textDepth == TextDepth::Fonts ? ReaderToolbarUi::TextView::Fonts
                     : textDepth == TextDepth::Spacing ? ReaderToolbarUi::TextView::Spacing
                     : textDepth == TextDepth::PointSize ? ReaderToolbarUi::TextView::PointSize
                                                        : ReaderToolbarUi::TextView::Rows;
    model.spacingPlace = textChoiceInUse(2);
    model.spacingDraftPermille = spacingDraftPermille;
    model.spacingLabel = [](int place) { return I18N.get(textChoiceLabel(2, place)); };
    model.numericDraft = pointSizeDraft.c_str();
    if (textDepth == TextDepth::PointSize) {
      const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
      if (!sizes.empty()) numericHint = std::to_string(sizes.front()) + "-" + std::to_string(sizes.back()) +
          " pt; " + tr(STR_DONE) + ": " + std::to_string(snapToNearestPointSize(sizes, enteredPointSize())) + " pt";
      model.numericHint = numericHint.c_str();
    }
#endif
    if (textDepth == TextDepth::Fonts) {
      model.panelTitle = tr(STR_FONT);
      model.itemCount = static_cast<int>(fontFamilies.size());
      model.sheetRows = kTextRowCount;  // the frame of the Text rows
      model.rowText = [this](int i) { return i < static_cast<int>(fontFamilies.size()) ? fontFamilies[i].ten : ""; };
      model.rowMarked = [this](int i) { return i == fontdoc::hoDangDung(&sdFontSystem.registry()); };
    } else {
      model.panelTitle = tr(STR_TOOL_TEXT);
      model.itemCount = kTextRowCount;
      model.rowText = [this](int i) { return textRowName(i); };
      model.rowValue = [this](int i) { return textRowValue(i); };
      model.rowPinned = [this](int i) { return readermenu::daGhim(pins, pinOfRow(i)); };
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
      model.rowOpens = [](int) { return true; };  // every Text row opens its list (Font its fonts)
#endif
    }
  }
  else if (overlay == Overlay::Favorites) {
    model.panelTitle = tr(STR_READER_TAB_FAVORITES);
    model.itemCount = static_cast<int>(favoriteRows.size());
    model.rowText = [this](int i) { return favoriteRowName(i); };
    model.rowValue = [this](int i) { return favoriteRowValue(i); };
    model.emptyText = tr(STR_READER_FAVORITES_EMPTY);
  }
  else {
    model.panelTitle = tr(STR_TOOL_MORE);
    model.itemCount = static_cast<int>(moreItems.size());
    model.rowText = [this](int i) { return moreRowName(i); };
    model.rowValue = [this](int i) { return moreRowValue(i); };
    model.rowPinned = [this](int i) { return readermenu::daGhim(pins, pinOfRow(i)); };
  }
  toolbarUi->setModel(model);
  toolbarUi->render();
  drawMenuChrome();
}

void EpubReaderActivity::handleOverlayInput() {
  if (!toolbarUi) return;

  // A modal option picker over the panel owns all input while open.
  if (overlayPopup.isActive()) {
    overlayPopup.handleInput(mappedInput, [this] {
      if (overlayPopup.isActive()) {
        paintOverlayPopup();  // highlight moved
        return;
      }
      // Dismissed or selected: erase the dialog -- clean page back, then the
      // panel over it (the dialog can overhang the sheet onto the page).
      RenderLock lock;
      settleOverlayRefresh();
      if (overlayPageStored) {
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
        renderOverlay();
        pushOverlayRefresh();
      } else {
        requestUpdate();
      }
    });
    return;
  }
  const auto fastRedraw = [this] {
    RenderLock lock;  // the render task shares the framebuffer
    settleOverlayRefresh();
    renderOverlay();
    pushOverlayRefresh();
  };

  // Jump to another spine item (chapter scrub). The overlay stays up and is
  // re-drawn over the new page by renderBook().
  const auto gotoSpine = [this](int target) {
    const int spineCount = epub->getSpineItemsCount();
    target = std::clamp(target, 0, spineCount - 1);
    if (target != currentSpineIndex) {
      RenderLock lock;
      clearDeferredReposition();
      nextPageNumber = 0;
      currentSpineIndex = target;
      section.reset();
    }
    requestUpdate();
  };
  const auto toolOverlay = [](int tool) {
    return tool == 0 ? Overlay::Contents : tool == 1 ? Overlay::Text : tool == 2 ? Overlay::More : Overlay::Favorites;
  };

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  // Mapped Back includes the native button, edge gesture and chrome target.
  // Handle it before FUI's consumed-touch return so the common foot Back advances one level.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (overlay == Overlay::Text && textDepth != TextDepth::Rows)
      leaveFontLevel();
    else
      closeOverlayToPage();
    return;
  }
#endif

  // Touch first: FreeInkUI routes the frame against the tap targets the last
  // render registered and hands back the action it mapped to.
  const auto routed = toolbarUi->route(mappedInput);

  // --- Toolbar ---
  if (overlay == Overlay::Toolbar) {
    switch (routed.event) {
      case ReaderToolbarUi::Event::Dismiss:
        closeOverlayToPage();
        return;
      case ReaderToolbarUi::Event::Tool:
        focusedTool = routed.value;
        openOverlay(toolOverlay(focusedTool));
        return;
      case ReaderToolbarUi::Event::PrevChapter:
        gotoSpine(currentSpineIndex - 1);
        return;
      case ReaderToolbarUi::Event::NextChapter:
        gotoSpine(currentSpineIndex + 1);
        return;
      case ReaderToolbarUi::Event::Scrub:
        gotoSpine(static_cast<int>((static_cast<float>(routed.permille) / 1000.0f) *
                                       static_cast<float>(epub->getSpineItemsCount() - 1) +
                                   0.5f));
        return;
      default:
        break;
    }
    if (routed.routed) return;  // a touch frame the chrome consumed (or dead space)

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeOverlayToPage();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      focusedTool = (focusedTool + kReaderTools - 1) % kReaderTools;
      panelCursorShown = true;
      fastRedraw();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      focusedTool = (focusedTool + 1) % kReaderTools;
      panelCursorShown = true;
      fastRedraw();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      openOverlay(toolOverlay(focusedTool));
      return;
    }
    const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
    const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (prev || next) {
      gotoSpine(currentSpineIndex + (next ? 1 : -1));
    }
    return;
  }

  // --- Panels (Contents / Text / More) ---
  const int count =
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
                    textDepth == TextDepth::Pick ? static_cast<int>(pick.labels.size()) :
#endif
                    overlay == Overlay::Contents ? epub->getTocItemsCount()
                    : overlay == Overlay::Text   ? (textDepth == TextDepth::Fonts ? static_cast<int>(fontFamilies.size()) : kTextRowCount)
                    : overlay == Overlay::Favorites ? static_cast<int>(favoriteRows.size())
                                                 : static_cast<int>(moreItems.size());
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
  const int pageRows = std::max(1, toolbarUi->visibleRows());
#endif

  // Activate the highlighted row: change a value / jump to a chapter / run an
  // action. Shared by the Confirm button and a row tap.
  const auto activateRow = [this, count] {
    if (panelIndex < 0 || panelIndex >= count) return;
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
    if (textDepth == TextDepth::Pick) {
      leavePick(/*keep=*/true);
      return;
    }
#endif
    if (overlay == Overlay::Text) {
      if (textDepth == TextDepth::Fonts) {
        chooseFontFamily(panelIndex);
      }
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
      else {
        openTextRow(panelIndex);
      }
#else
      else if (panelIndex == 0) {
        enterFontLevel();
      } else {
        openPick(textRowId(panelIndex), textRowName(panelIndex));
      }
#endif
    } else if (overlay == Overlay::Contents) {
      const auto item = epub->getTocItem(panelIndex);
      if (item.spineIndex != -1) {
        RenderLock lock;
        clearDeferredReposition();
        currentSpineIndex = item.spineIndex;
        pendingAnchor = item.anchor;
        nextPageNumber = 0;
        section.reset();
      }
      {
        RenderLock lock;
        overlay = Overlay::None;
        discardOverlayPage();
        panelClosedLocked(false, false);
      }
      requestUpdate();
    } else if (overlay == Overlay::More) {
      activateMoreRow(panelIndex);
    }
    else if (overlay == Overlay::Favorites) {
      activateFavoriteRow(panelIndex);
    }
  };

  // Steps up to the toolbar -- the Back button and a tap on the page above
  // the sheet.
  const auto dismissPanel = [this, &fastRedraw] {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    if (overlay == Overlay::Text && textDepth != TextDepth::Rows) leaveFontLevel();
    else closeOverlayToPage();
    return;
#endif
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
    if (textDepth == TextDepth::Pick) {
      leavePick(/*keep=*/false);  // one level up, the value in use kept
      return;
    }
#endif
    if (overlay == Overlay::Text && textDepth == TextDepth::Fonts) {
      leaveFontLevel();  // one level up: the Text rows
      return;
    }
    overlay = Overlay::Toolbar;
    // Restore the snapshotted page under the toolbar instead of re-rendering
    // it (2+ refreshes -> one FAST). Re-store right away so another panel
    // round-trip can restore again.
    if (overlayPageStored) {
      {
        RenderLock lock;  // the render task shares the framebuffer
        settleOverlayRefresh();
        // No baseline resync: the glass shows the panel, and erasing it needs
        // the differential to keep diffing against the last pushed frame.
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
      }
      fastRedraw();  // takes its own RenderLock
      return;
    }
    requestUpdate();
  };

  // Pages the list by one screen of rows through the nav (measured page size,
  // no-op at the ends). A shown cursor rides along so the buttons continue
  // from what is visible; on touch boards only the viewport moves.
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
  const auto pageList = [this, count, pageRows, &fastRedraw](int direction) {
    if (count <= 0) return;
    const bool moved = toolbarUi->nav().scrollBy(direction * pageRows, count);
    if (panelCursorShown) {
      panelIndex = std::clamp(panelIndex + direction * pageRows, 0, count - 1);
      fastRedraw();
      return;
    }
    if (moved) fastRedraw();
  };
#endif

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (overlay == Overlay::Text && textDepth == TextDepth::Spacing) {
    if (routed.event == ReaderToolbarUi::Event::SpacingDraft || routed.event == ReaderToolbarUi::Event::SpacingCommit) {
      bool draftChanged;
      {
        RenderLock lock;
        const int draft = std::clamp(routed.permille, 0, 1000);
        draftChanged = draft != spacingDraftPermille;
        spacingDraftPermille = draft;
        spacingDragging = true;
      }
      if (routed.event == ReaderToolbarUi::Event::SpacingDraft) {
        if (draftChanged) fastRedraw();
        return;
      }
    }
    if (spacingDragging && mappedInput.wasScreenTouchReleased()) {
      int place;
      {
        RenderLock lock;
        spacingDragging = false;
        place = (spacingDraftPermille * 4 + 500) / 1000;
        spacingDraftPermille = place * 250;
      }
      const bool changed = place != textChoiceInUse(2);
      chooseTextValue(2, place);
      if (!changed) fastRedraw();
      return;
    }
  }
#endif

  switch (routed.event) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    case ReaderToolbarUi::Event::SizeStep:
      if (overlay == Overlay::Text && textDepth == TextDepth::Rows) stepMenuPointSize(routed.value);
      return;
    case ReaderToolbarUi::Event::SizeEntry:
      if (overlay == Overlay::Text && textDepth == TextDepth::Rows) enterTextDepth(TextDepth::PointSize);
      return;
    case ReaderToolbarUi::Event::NumericKey:
      if (overlay == Overlay::Text && textDepth == TextDepth::PointSize) {
        if (routed.value == 11) {
          if (!pointSizeDraft.empty()) {
            const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
            applyMenuPointSize(snapToNearestPointSize(sizes, enteredPointSize()));
          }
          leaveFontLevel();
        } else {
          {
            RenderLock lock;
            if (routed.value == 10) { if (!pointSizeDraft.empty()) pointSizeDraft.pop_back(); }
            else if (routed.value >= 0 && routed.value <= 9 && pointSizeDraft.size() < 3) pointSizeDraft += static_cast<char>('0' + routed.value);
          }
          fastRedraw();
        }
      }
      return;
#endif
    case ReaderToolbarUi::Event::Dismiss:
      dismissPanel();
      return;
    case ReaderToolbarUi::Event::Tool: {
      // Sheet-bottom tool switcher: hop straight to another panel.
      const Overlay target = toolOverlay(routed.value);
      if (target != overlay) {
        focusedTool = routed.value;
        openOverlay(target);
      }
      return;
    }
    case ReaderToolbarUi::Event::Row:
      // A tap on the right-edge strip pages the sheet instead (upper half =
      // previous page, lower half = next): swipes are unreliable on etched
      // glass, and a long contents list needs a fast way through.
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
      if (routed.x >= renderer.getScreenWidth() - 44) {
        pageList(routed.y >= renderer.getScreenHeight() - (renderer.getScreenHeight() * 62) / 200 ? 1 : -1);
        return;
      }
#endif
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
      if (routed.hold) {  // a hold pins the row to Favorites, or takes it off
        togglePin(pinOfRow(routed.value));
        fastRedraw();
        return;
      }
#endif
      panelIndex = routed.value;
      panelCursorShown = false;
      activateRow();
      return;
    default:
      break;
  }
  // Swipe up/down pages the list. Checked before the routed-frame return:
  // FUI routes every touch frame over the sheet, so a swipe's frames count as
  // routed (without dispatching -- too much travel for a tap) and the gesture
  // would otherwise never be seen.
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  const bool listView = overlay != Overlay::Text || textDepth == TextDepth::Rows || textDepth == TextDepth::Fonts;
  const int rowDelta = listView ? toolbarUi->scrollRows(mappedInput, count) : 0;
  if (rowDelta != 0) {
    panelCursorShown = false;
    if (toolbarUi->nav().scrollBy(rowDelta, count)) fastRedraw();
    return;
  }
#else
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    pageList(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
    return;
  }
#endif
  if (routed.routed) return;  // consumed by the chrome (title band, dead space)

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    dismissPanel();
    return;
  }

#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
  // Select held on a Text, More or Favorites row pins it or takes it off, as in the list menu. The hold
  // swallows the release that ends it, so the row does not also open.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, readermenu::GIU_GHIM_MS)) {
    const uint8_t pin = pinOfRow(panelIndex);
    if (pin == 0xFF) return;
    togglePin(pin);
    RenderLock lock;
    redrawSheetLocked();  // Favorites may lose a row, and its sheet that row's height
    return;
  }
#endif

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRow();
    return;
  }

#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
  // Buttons (founder 06/10): the side buttons go to the tab before or after; the front buttons move the
  // cursor. A second level (the font list) keeps the side buttons still: Back leaves it.
  const bool tabBefore = mappedInput.wasReleased(MappedInputManager::Button::Up);
  if (tabBefore || mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (textDepth != TextDepth::Rows) return;
    focusedTool = (focusedTool + (tabBefore ? kReaderTools - 1 : 1)) % kReaderTools;
    openOverlay(toolOverlay(focusedTool));
    return;
  }
  constexpr auto kRowBefore = MappedInputManager::Button::Left;
  constexpr auto kRowAfter = MappedInputManager::Button::Right;
#endif

  // The cursor buttons (X4 Pro: Up/Down and Left/Right; buttons: the front pair) step one row, holding
  // past PANEL_HOLD_MS jumps PANEL_HOLD_STEP rows in one go, which
  // is how you cross a hundreds-of-chapters contents list without a press per
  // row. The jump fires once on the hold and swallows the release that ends it,
  // so it never doubles up with the tap step.
  if (count > 0) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    const bool up = mappedInput.isPressed(MappedInputManager::Button::Up) ||
                    mappedInput.isPressed(MappedInputManager::Button::Left);
    const bool down = mappedInput.isPressed(MappedInputManager::Button::Down) ||
                      mappedInput.isPressed(MappedInputManager::Button::Right);
#else
    const bool up = mappedInput.isPressed(kRowBefore);
    const bool down = mappedInput.isPressed(kRowAfter);
#endif
    if (!panelHoldJumped && (up || down) && mappedInput.getHeldTime() >= PANEL_HOLD_MS) {
      const int step = down ? PANEL_HOLD_STEP : -PANEL_HOLD_STEP;
      panelIndex = std::clamp(panelIndex + step, 0, count - 1);
      panelHoldJumped = true;
      panelCursorShown = true;
      fastRedraw();
      return;
    }

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    const bool releasedUp = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool releasedDown = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);
#else
    const bool releasedUp = mappedInput.wasReleased(kRowBefore);
    const bool releasedDown = mappedInput.wasReleased(kRowAfter);
#endif
    if (releasedUp || releasedDown) {
      if (!panelHoldJumped) {
        panelIndex = releasedUp ? ButtonNavigator::previousIndex(panelIndex, count)
                                : ButtonNavigator::nextIndex(panelIndex, count);
        panelCursorShown = true;
        fastRedraw();
      }
      panelHoldJumped = false;
    }
  }
}

// First paint of the option picker over the panel (and highlight repaints).
// The dialog draws over the current framebuffer without clearing; erasing it
// on dismissal is the popup gate's restore in handleOverlayInput().
void EpubReaderActivity::paintOverlayPopup() {
  RenderLock lock;
  settleOverlayRefresh();
  overlayPopup.render(renderer);
  pushOverlayRefresh();
}

void EpubReaderActivity::danLaiTrang() {
  if (section) {
    rememberCurrentContentOffset();
    // The preview stayed on screen after the chapter landed: its first character, not the start of
    // the landed page, so the place does not creep back with every change.
    if (xemTruocTrenMan) cachedVisibleTextOffset = xemTruocDich;
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->pageCount;
    nextPageNumber = section->currentPage;
    section->abandonBuild();  // laid out under the settings just replaced: nothing to keep
  }
  dropCatchUp();  // the layout of the previous request, if any, goes the same way
  section.reset();  // dan lai trang voi chu moi o lan ve tiep theo
  xemTruocTrenMan = false;
  xemTruocLat = 0;
  catchUpFails = 0;
  xemTruoc = cachedVisibleTextOffset.has_value() && cachedSpineIndex == currentSpineIndex;
  if (xemTruoc) xemTruocDich = *cachedVisibleTextOffset;
  xemTruocInputMs = millis();
}

void EpubReaderActivity::dropCatchUp() {
  if (!catchUp) return;
  catchUp->abandonBuild();  // removes its .part files; the committed cache is untouched
  catchUp.reset();
}

// When the preview's layout may take a step. The loop skips its delay only while this holds, so a
// layout waiting out the quiet time, given up on, or parked for the radio does not spin the loop.
// With the radio holding the heap it stays parked as every background build does
// (deferBackgroundBuildForBle): the first turn lays the chapter out instead.
bool EpubReaderActivity::catchUpCanTick() const {
  // A turn asked from the preview waits for the paint, which lays the chapter out and turns.
  return xemTruoc && !section && epub && buildViewportWidth != 0 && xemTruocLat == 0 &&
         millis() - xemTruocInputMs >= CATCH_UP_QUIET_MS &&
         catchUpFails < CATCH_UP_MAX_FAILS && !deferBackgroundBuildForBle() && !bleturner::status().starting;
}

// Loop task, between key presses: lays the chapter out under the current settings ~20 ms at a time
// until it reaches the page being read, then puts it in place without drawing anything.
void EpubReaderActivity::catchUpTick(const bool inputThisPass) {
  if (inputThisPass) xemTruocInputMs = millis();
  if (!catchUpCanTick()) return;
  RenderLock lock(RenderLock::TryTake{});
  if (!lock.acquired()) return;
  HalPowerManager::Lock fullSpeed;  // the loop runs down-clocked after 3 s without a key
  const ReaderRenderSpec spec = SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight);
  const uint32_t target = xemTruocDich;
  // Every step stays above the floor the background build keeps, not only the first.
  if (ESP.getFreeHeap() < BACKGROUND_BUILD_MIN_FREE_HEAP || ESP.getMaxAllocHeap() < BACKGROUND_BUILD_MIN_MAX_ALLOC) {
    xemTruocInputMs = millis();
    return;
  }
  if (!catchUp) {
    catchUp.reset(new Section(epub, currentSpineIndex, renderer, preview));
    const bool loaded = catchUp->loadSectionFile(spec);
    if (!(loaded && (!catchUp->isPartial() || catchUp->coversVisibleTextOffset(target))) && !catchUp->startBuild(spec)) {
      catchUp.reset();
      ++catchUpFails;  // a few tries, then the next turn lays it out itself
      xemTruocInputMs = millis();
      return;
    }
  } else if (catchUp->isBuilding() && !catchUp->buildSomeMore(1)) {
    if (!catchUp->buildStarved()) catchUp.reset();
    ++catchUpFails;
    xemTruocInputMs = millis();
    return;
  }
  if (catchUp->isBuilding() && !catchUp->buildReachedVisibleTextOffset(target)) return;
  const auto page = catchUp->getPageForVisibleTextOffset(target);
  if (!page) {
    dropCatchUp();
    return;
  }
  catchUp->currentPage = *page;
  if (catchUp->isBuilding()) catchUp->parkBuild();  // hands the parser's heap back; resumes on demand
  section = std::move(catchUp);
  nextPageNumber = section->currentPage;
  xemTruoc = false;
  xemTruocTrenMan = true;
  LOG_INF("ERS", "CATCH_UP landed spine=%d page=%d pages=%u", currentSpineIndex, section->currentPage,
          static_cast<unsigned>(section->pageCount));
}

// Draws the preview page (and the open panel over it) in place of a laid-out one. False when there is
// no preview to draw: the caller lays the chapter out as before.
bool EpubReaderActivity::renderPreview(const int marginTop, const int marginRight, const int marginBottom,
                                      const int marginLeft) {
  if (catchUp && catchUp->isBuilding() && !catchUp->isBuildParked() && !catchUp->parkBuild()) dropCatchUp();
  const ReaderRenderSpec spec = SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight);
  std::unique_ptr<Page> page;
  {
    Section chapter(epub, currentSpineIndex, renderer, preview);
    page = chapter.previewPage(spec, xemTruocDich);
  }
  if (!page) return false;
  settleBuildPopup();
  renderer.clearScreen();
  currentPageVisibleOffset = page->visibleTextOffset;
  currentPageFootnotes = std::move(page->footnotes);
  currentPageLinks = std::move(page->links);
  currentPageLinkMarginLeft = marginLeft;
  currentPageLinkMarginTop = marginTop;
  discardOverlayPage();
  paintDropped.store(false, std::memory_order_relaxed);
  renderContents(std::move(page), marginTop, marginRight, marginBottom, marginLeft);
  if (paintDropped) return true;
  lastRenderCompleteMs = millis();
  if (overlay != Overlay::None && usesToolbarMenu()) {
    overlayPageStored = renderer.storeBwBuffer();
    renderOverlay();
    if (overlayPopup.isActive()) overlayPopup.render(renderer);
    pushOverlayRefresh();
  }
  pageFrameUsb = gpio.isUsbConnected();
  pageFrameShown = true;
  return true;
}

bool EpubReaderActivity::docCoChuMotNac(const int huong) {
  const std::vector<uint8_t> sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
  if (sizes.empty()) return false;
  const int cur = fontdoc::coDangDung(sizes);
  int moi = cur + huong;
  if (moi < 0) moi = 0;
  if (moi >= static_cast<int>(sizes.size())) moi = static_cast<int>(sizes.size()) - 1;
  if (moi == cur) return false;  // da o bien: nhip giu bi tieu, khong lat trang, khong doi gi
  {
    RenderLock lock;
    fontdoc::apCo(renderer, sizes[moi]);
    danLaiTrang();
  }
  SETTINGS.saveToFile();
  return true;
}

// Font level of the Text panel. Both steps change one level inside the same sheet: the old chrome is
// wiped back to the clean page (no refresh) and the new level is pushed in one fast refresh.
void EpubReaderActivity::enterFontLevel() {
  RenderLock lock;  // the render task shares the framebuffer and family list
  fontFamilies = fontdoc::danhSachHo(&sdFontSystem.registry());
  textDepth = TextDepth::Fonts;
  panelIndex = fontdoc::hoDangDung(&sdFontSystem.registry());
  toolbarUi->nav().reset(panelIndex);
  if (!panelCursorShown) toolbarUi->nav().top = panelIndex;
  settleOverlayRefresh();
  if (overlayPageStored) {
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = renderer.storeBwBuffer();
  }
  renderOverlay();
  pushOverlayRefresh();
}

void EpubReaderActivity::leaveFontLevel() {
  RenderLock lock;  // the render task may be reading the family list
  textDepth = TextDepth::Rows;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  spacingDragging = false;
  pointSizeDraft.clear();
#endif
  fontFamilies.clear();
  fontFamilies.shrink_to_fit();
  panelIndex = 0;
  toolbarUi->nav().reset();
  settleOverlayRefresh();
  if (overlayPageStored) {
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = renderer.storeBwBuffer();
  }
  renderOverlay();
  pushOverlayRefresh();
}

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
uint8_t EpubReaderActivity::enteredPointSize() const {
  int value = 0;
  for (const char digit : pointSizeDraft) value = std::min(255, value * 10 + digit - '0');
  return static_cast<uint8_t>(value);
}

void EpubReaderActivity::enterTextDepth(const TextDepth depth) {
  RenderLock lock;
  textDepth = depth;
  spacingDragging = false;
  spacingDraftPermille = textChoiceInUse(2) * 250;
  pointSizeDraft = depth == TextDepth::PointSize ? std::to_string(SETTINGS.fontPointSize) : "";
  toolbarUi->nav().reset();
  settleOverlayRefresh();
  if (overlayPageStored) {
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = renderer.storeBwBuffer();
  }
  renderOverlay();
  pushOverlayRefresh();
}

void EpubReaderActivity::applyMenuPointSize(const uint8_t pointSize) {
  {
    RenderLock lock;
    if (pointSize == SETTINGS.fontPointSize) return;
    fontdoc::apCo(renderer, pointSize);
    invalidateTextSettingsLocked();
  }
  applyTextSettingLive();
}

void EpubReaderActivity::stepMenuPointSize(const int direction) {
  const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
  if (sizes.empty()) return;
  const int current = fontdoc::coDangDung(sizes);
  const int next = std::clamp(current + direction, 0, static_cast<int>(sizes.size()) - 1);
  applyMenuPointSize(sizes[next]);
}
#endif

// A family picked in the list: the page above the sheet is laid out again in it (the preview) and
// the sheet stays on the list.
void EpubReaderActivity::chooseFontFamily(const int index) {
  bool changed = false;
  {
    RenderLock lock;
    if (index == fontdoc::hoDangDung(&sdFontSystem.registry())) return;
    changed = fontdoc::apHo(renderer, &sdFontSystem.registry(), index);
    if (changed) invalidateTextSettingsLocked();
  }
  if (changed) applyTextSettingLive();
}

void EpubReaderActivity::panelClosed(const bool leaving, const bool frameUp) {
  RenderLock lock;
  panelClosedLocked(leaving, frameUp);
}

void EpubReaderActivity::flushTextSettings() {
  RenderLock lock;
  flushTextSettingsLocked();
}

void EpubReaderActivity::invalidateTextSettingsLocked() {
  textCloseFrame.store(0, std::memory_order_relaxed);
  textSettingsDirty = true;
  danLaiTrang();
  discardOverlayPage();
}

void EpubReaderActivity::applyReaderTextSettingsLocked() {
  sdFontSystem.ensureLoaded(renderer);
  invalidateTextSettingsLocked();
}

void EpubReaderActivity::markClosedTextFrameUpLocked() {
  if (overlay != Overlay::None || paintDropped) return;
  uint8_t waiting = 1;
  textCloseFrame.compare_exchange_strong(waiting, 2, std::memory_order_release, std::memory_order_relaxed);
}

void EpubReaderActivity::flushTextSettingsLocked() {
  textCloseFrame.store(0, std::memory_order_relaxed);
  if (!textSettingsDirty) return;
  textSettingsDirty = false;
  SETTINGS.saveToFile();
}

void EpubReaderActivity::panelClosedLocked(const bool leaving, const bool frameUp) {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  tenorchrome::noteReaderFootBar(false, false, -1);
  spacingDragging = false;
  pointSizeDraft.clear();
#endif
  textDepth = TextDepth::Rows;
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
  pick = Pick{};
#endif
  if (leaving) {
    textCloseFrame.store(0, std::memory_order_relaxed);
    if (textSettingsDirty) {
      textSettingsDirty = false;
      if (activityManager.isSleepTransition())
        SETTINGS.saveToFile();
      else
        activityManager.deferWrite([] { SETTINGS.saveToFile(); });
    }
    return;
  }
  fontFamilies.clear();
  fontFamilies.shrink_to_fit();
  sdFontSystem.releaseCatalog();
  if (frameUp)
    flushTextSettingsLocked();
  else if (textSettingsDirty)
    textCloseFrame.store(1, std::memory_order_relaxed);
}

void EpubReaderActivity::applyReaderTextSettings() {
  RenderLock lock;
  applyReaderTextSettingsLocked();
}

// The More panel carries everything the classic list menu offers except the
// two entries that have their own tool (chapters -> Contents, text -> Text).
void EpubReaderActivity::buildMoreActions() {
  readermenu::buildMoreItems(moreItems, !currentPageFootnotes.empty(), !cachedBookmarks.empty(), Frontlight.present());
}

std::string EpubReaderActivity::moreRowName(int row) const {
  return row >= 0 && row < static_cast<int>(moreItems.size()) ? I18N.get(moreItems[row].labelId) : "";
}

std::string EpubReaderActivity::moreRowValue(int row) const {
  using MA = EpubReaderMenuActivity::MenuAction;
  static constexpr StrId kOrient[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED,
                                      StrId::STR_LANDSCAPE_CCW};
  static_assert(std::size(kOrient) == CrossPointSettings::ORIENTATION_COUNT, "orientation labels");
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return "";
  switch (moreItems[row].action) {
    case MA::ROTATE_SCREEN:
      return I18N.get(kOrient[SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT]);
    case MA::AUTO_PAGE_TURN:
      return (autoTurnOption == 0 || autoTurnOption >= static_cast<int>(std::size(PAGE_TURN_RATES)))
                 ? std::string(tr(STR_STATE_OFF))
                 : std::to_string(PAGE_TURN_RATES[autoTurnOption]);
    case MA::NIGHT_MODE:
      return SETTINGS.screenInverted ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case MA::FRONTLIGHT:
      return Frontlight.isOn() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

void EpubReaderActivity::activateMoreRow(int row) {
  using MA = EpubReaderMenuActivity::MenuAction;
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return;
  {
    RenderLock lock;  // several actions launch screens that paint the framebuffer
    settleOverlayRefresh();
  }
  const auto action = moreItems[row].action;
  // In-place toggles keep the panel open and re-render the page beneath it.
  switch (action) {
#if !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
    case MA::STATUS_BAR:
      // Its modes in place, as the list menu offers them; the toolbar path used to close the menu and do
      // nothing (no case for it after the menu).
      openPick(PICK_MORE + static_cast<int>(action), moreRowName(row));
      return;
#else
    case MA::STATUS_BAR:  // the touch toolbar had the same fall-through: its modes in the popup of Orientation
      overlayPopup.show(StrId::STR_HIDE_READER_STATUS_BAR, readermenu::STATUS_BAR_MODE_LABELS,
                        CrossPointSettings::READER_STATUS_BAR_MODE_COUNT, SETTINGS.readerStatusBarMode,
                        [this](int idx) { setReaderStatusBarMode(idx); });
      paintOverlayPopup();
      return;
#endif
    case MA::ROTATE_SCREEN: {
      static constexpr StrId kOrientIds[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW,
                                             StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};
      static_assert(std::size(kOrientIds) == CrossPointSettings::ORIENTATION_COUNT, "orientation options");
      overlayPopup.show(StrId::STR_ORIENTATION, kOrientIds, static_cast<int>(std::size(kOrientIds)),
                        SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT, [this](int idx) {
                          if (idx == SETTINGS.orientation) return;
                          applyOrientation(static_cast<uint8_t>(idx));
                          // The stored page is laid out for the old orientation.
                          discardOverlayPage();
                          requestUpdate();
                        });
      paintOverlayPopup();
      return;
    }
    case MA::AUTO_PAGE_TURN: {
      std::vector<std::string> labels;
      labels.reserve(std::size(PAGE_TURN_RATES));
      labels.emplace_back(tr(STR_STATE_OFF));
      for (size_t i = 1; i < std::size(PAGE_TURN_RATES); ++i) labels.push_back(std::to_string(PAGE_TURN_RATES[i]));
      overlayPopup.show(StrId::STR_AUTO_TURN_PAGES_PER_MIN, labels, autoTurnOption, [this](int idx) {
        autoTurnOption = idx;
        toggleAutoPageTurn(static_cast<uint8_t>(idx));
      });
      paintOverlayPopup();
      return;
    }
    case MA::NIGHT_MODE:
      SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
      SETTINGS.saveToFile();
      discardOverlayPage();
      requestUpdate();
      return;
    case MA::FRONTLIGHT: {
      const bool lightOn = !Frontlight.isOn();
      Frontlight.setOn(lightOn);
      SETTINGS.frontlightOn = lightOn ? 1 : 0;
      SETTINGS.saveToFile();
      {
        RenderLock lock;  // the render task shares the framebuffer
        renderOverlay();
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      }
      return;
    }
    default:
      break;
  }
  // Leaf actions open their own screen / perform the action; close the overlay first.
  {
    RenderLock lock;
    overlay = Overlay::None;
    panelClosedLocked(true, false);
  }
  if (action == MA::GO_TO_PERCENT && overlayPageStored) {
    // The percent dialog is a popup over the current frame: wipe the toolbar
    // chrome back to the clean page first so the dialog draws over the page,
    // not the sheet. No refresh push — the dialog's first frame carries it.
    RenderLock lock;
    settleOverlayRefresh();
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = false;
  } else {
    discardOverlayPage();
  }
  if (action == MA::TOGGLE_BOOKMARK) {
    if (waitsForIndex()) return;
    // No child activity here to trigger the re-render the list menu relies on:
    // show the same confirmation popup the long-press path does.
    addBookmark();
    showBookmarkMessage = true;
    bookmarkMessageTime = millis();
    requestUpdate();
    return;
  }
  // Bang More cua thanh cong cu cham: khong co lua chon tai cho, nen Co chu va Font chu di
  // duong mo man Cai dat van ban (coChu 0, hoFont -1).
  onReaderMenuConfirm(action, MenuResult{});
  // Actions that neither open a screen nor leave the reader (a sync with no
  // credentials, say) would otherwise leave the closed panel on screen.
  if (action != MA::GO_HOME && action != MA::DELETE_CACHE && action != MA::FILE_TRANSFER) requestUpdate();
}

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
void EpubReaderActivity::openTextRow(const int row) {
  if (row == 0)
    enterFontLevel();
  else if (row == 1)
    enterTextDepth(TextDepth::PointSize);
  else if (row == 2)
    enterTextDepth(TextDepth::Spacing);
  else
    cycleTextRow(row);
}
#endif

void EpubReaderActivity::loadPins() {
  const int count = std::min<int>(SETTINGS.readerFavoriteCount, CrossPointSettings::READER_FAVORITE_MAX);
  pins.assign(SETTINGS.readerFavorites, SETTINGS.readerFavorites + count);
  // Never pinned: the size and the sync, as the touch list menu starts; on buttons the sync, as the list
  // menu of the button boards starts (readermenu::DEFAULT_FAVORITES).
  if (pins.empty() && !SETTINGS.readerFavoritesDaDat) {
    if (tenorchrome::kTouchShell)
      pins = {static_cast<uint8_t>(readermenu::PIN_TEXT | 1), static_cast<uint8_t>(readermenu::Action::SYNC)};
    else
      for (const auto action : readermenu::DEFAULT_FAVORITES) pins.push_back(static_cast<uint8_t>(action));
  }
  buildMoreActions();
  favoriteRows.clear();
  for (const uint8_t pin : pins) {
    const bool text = pin & readermenu::PIN_TEXT;
    const bool here = text ? textRowPlace(pin & ~readermenu::PIN_TEXT) >= 0
                           : std::any_of(moreItems.begin(), moreItems.end(), [pin](const auto& item) {
                               return static_cast<uint8_t>(item.action) == pin;
                             });
    if (here) favoriteRows.push_back(pin);  // an action this book lacks (footnotes) waits unshown
  }
}

void EpubReaderActivity::togglePin(const uint8_t pin) {
  if (pin == 0xFF) return;
  {
    RenderLock lock;
    if (!readermenu::doiGhim(pins, pin)) return;  // full: 8 pins
    std::copy(pins.begin(), pins.end(), SETTINGS.readerFavorites);
    SETTINGS.readerFavoriteCount = static_cast<uint8_t>(pins.size());
    SETTINGS.readerFavoritesDaDat = 1;
    loadPins();
    if (overlay == Overlay::Favorites) panelIndex = std::min(panelIndex, std::max(0, static_cast<int>(favoriteRows.size()) - 1));
  }
  SETTINGS.saveToFile();
}

uint8_t EpubReaderActivity::pinOfRow(const int row) const {
  if (textDepth != TextDepth::Rows) return 0xFF;
  if (overlay == Overlay::Text && textDepth == TextDepth::Rows && row >= 0 && row < kTextRowCount)
    return static_cast<uint8_t>(readermenu::PIN_TEXT | textRowId(row));
  if (overlay == Overlay::More && row >= 0 && row < static_cast<int>(moreItems.size()))
    return static_cast<uint8_t>(moreItems[row].action);
  if (overlay == Overlay::Favorites && row >= 0 && row < static_cast<int>(favoriteRows.size())) return favoriteRows[row];
  return 0xFF;
}

namespace {
// The More row of an action pin, or -1.
int moreRowOf(const std::vector<EpubReaderMenuActivity::MenuItem>& items, const uint8_t pin) {
  for (size_t i = 0; i < items.size(); ++i)
    if (static_cast<uint8_t>(items[i].action) == pin) return static_cast<int>(i);
  return -1;
}
}  // namespace

std::string EpubReaderActivity::favoriteRowName(const int row) const {
  if (row < 0 || row >= static_cast<int>(favoriteRows.size())) return "";
  const uint8_t pin = favoriteRows[row];
  if (pin & readermenu::PIN_TEXT) return textRowName(textRowPlace(pin & ~readermenu::PIN_TEXT));
  return moreRowName(moreRowOf(moreItems, pin));
}

std::string EpubReaderActivity::favoriteRowValue(const int row) const {
  if (row < 0 || row >= static_cast<int>(favoriteRows.size())) return "";
  const uint8_t pin = favoriteRows[row];
  if (pin & readermenu::PIN_TEXT) return textRowValue(textRowPlace(pin & ~readermenu::PIN_TEXT));
  return moreRowValue(moreRowOf(moreItems, pin));
}

void EpubReaderActivity::activateFavoriteRow(const int row) {
  if (row < 0 || row >= static_cast<int>(favoriteRows.size())) return;
  const uint8_t pin = favoriteRows[row];
  if (!(pin & readermenu::PIN_TEXT)) {
    activateMoreRow(moreRowOf(moreItems, pin));
    return;
  }
  const int place = textRowPlace(pin & ~readermenu::PIN_TEXT);
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (place > 2) {
    cycleTextRow(place);  // stepped here, the page under the panel shows it
    return;
  }
  // Font, size and line spacing open their level of the Text panel.
  focusedTool = 1;
  openOverlay(Overlay::Text);
  panelIndex = place;
  openTextRow(place);
#else
  if (place > 0) {
    openPick(textRowId(place), textRowName(place));  // its list over Favorites; Back comes back here
    return;
  }
  // Font opens its list in the Text panel.
  focusedTool = 1;
  openOverlay(Overlay::Text);
  enterFontLevel();
#endif
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  if (!epub) return;

  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';
  int targetSpineIndex = sameFile ? currentSpineIndex : epub->resolveHrefToSpineIndex(hrefStr);

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    return;
  }

  if (savePosition) {
    // Full: drop the oldest entry so Back always returns from the newest jump.
    if (footnoteDepth == MAX_FOOTNOTE_DEPTH) {
      std::copy(savedPositions + 1, savedPositions + MAX_FOOTNOTE_DEPTH, savedPositions);
      footnoteDepth--;
    }
    // A child screen (menu, footnote list) may have released the section;
    // nextPageNumber then holds the page it was on.
    const int page = section ? section->currentPage : nextPageNumber;
    savedPositions[footnoteDepth] = {currentSpineIndex, page};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, page);
  }

  {
    RenderLock lock;
    clearDeferredReposition();
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

namespace {
// The back-stack as the screen after the reader writes it (ActivityManager::deferWrite takes a plain
// function). Empty bytes: the file is only to be removed.
std::string stagedLinksPath;
std::string stagedLinksBytes;

void writeStagedLinks() {
#ifdef TENOR_PRESS_PROBE
  const unsigned long started = millis();
#endif
  const std::string path = std::move(stagedLinksPath);
  const std::string bytes = std::move(stagedLinksBytes);
  stagedLinksPath.clear();
  stagedLinksBytes.clear();
  if (bytes.empty()) {
    Storage.remove(path.c_str());
  } else {
    // Whole under a temporary name, then renamed (SdFat's rename does not overwrite): a cut write
    // leaves a stray .tmp, never a torn links.bin.
    const std::string part = path + ".tmp";
    HalFile f;
    bool ok = Storage.openFileForWrite("ERS", part, f) &&
              f.write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) == bytes.size();
    f.close();
    if (ok) Storage.remove(path.c_str());
    ok = ok && Storage.rename(part.c_str(), path.c_str());
    if (!ok) {
      LOG_ERR("ERS", "Failed to write link stack");
      Storage.remove(part.c_str());
    }
  }
#ifdef TENOR_PRESS_PROBE
  LOG_INF("ERS", "%s bytes=%u ms=%lu", bytes.empty() ? "LINKS_DROP" : "LINKS_SAVE", static_cast<unsigned>(bytes.size()),
          millis() - started);
#endif
}
}  // namespace

void EpubReaderActivity::saveLinkStack() const {
  // Nothing in the stack and no file left from the open: nothing to do.
  if (footnoteDepth == 0 && !linkStackOnCard) return;
  stagedLinksPath = epub->getCachePath() + "/links.bin";
  stagedLinksBytes.clear();
  if (footnoteDepth > 0) {
    // [depth] then depth x (spine u16 LE, page u16 LE)
    stagedLinksBytes.push_back(static_cast<char>(footnoteDepth));
    for (int i = 0; i < footnoteDepth; i++) {
      const SavedPosition& pos = savedPositions[i];
      stagedLinksBytes.push_back(static_cast<char>(pos.spineIndex & 0xFF));
      stagedLinksBytes.push_back(static_cast<char>((pos.spineIndex >> 8) & 0xFF));
      stagedLinksBytes.push_back(static_cast<char>(pos.pageNumber & 0xFF));
      stagedLinksBytes.push_back(static_cast<char>((pos.pageNumber >> 8) & 0xFF));
    }
  }
  // Sleep powers the card down next; any other exit leaves it for after the next screen's frame.
  if (activityManager.isSleepTransition()) {
    writeStagedLinks();
  } else {
    activityManager.deferWrite(writeStagedLinks);
  }
}

// Reads only. The file stays until the first frame is up (loop()), so open writes nothing to the
// card in front of that frame.
void EpubReaderActivity::loadLinkStack() {
#ifdef TENOR_PRESS_PROBE
  const unsigned long started = millis();
#endif
  const std::string path = epub->getCachePath() + "/links.bin";
  if (!Storage.exists(path.c_str())) {
#ifdef TENOR_PRESS_PROBE
    LOG_INF("ERS", "LINKS_LOAD depth=0 ms=%lu", millis() - started);
#endif
    return;
  }
  linkStackOnCard = true;
  {
    HalFile f;
    uint8_t data[1 + MAX_FOOTNOTE_DEPTH * 4];
    if (Storage.openFileForRead("ERS", path, f)) {
      const int size = f.read(data, sizeof(data));
      const int depth = size > 0 ? data[0] : 0;
      if (depth >= 1 && depth <= MAX_FOOTNOTE_DEPTH && size == 1 + depth * 4) {
        const int spineCount = epub->getSpineItemsCount();
        bool valid = true;
        for (int i = 0; i < depth; i++) {
          const uint8_t* p = data + 1 + i * 4;
          savedPositions[i] = {p[0] | (p[1] << 8), p[2] | (p[3] << 8)};
          valid = valid && savedPositions[i].spineIndex < spineCount;
        }
        if (valid) {
          footnoteDepth = depth;
          LOG_DBG("ERS", "Loaded link stack, depth %d", depth);
        }
      }
    }
  }
#ifdef TENOR_PRESS_PROBE
  LOG_INF("ERS", "LINKS_LOAD depth=%d ms=%lu", footnoteDepth, millis() - started);
#endif
}

void EpubReaderActivity::restoreSavedPosition() {
  if (footnoteDepth <= 0) return;
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock;
    clearDeferredReposition();
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::loadCachedBookmarks() {
  cachedBookmarks.clear();
  if (cachedBookmarks.capacity() < initialBookmarkCacheCapacity) {
    cachedBookmarks.reserve(initialBookmarkCacheCapacity);
  }
  if (!epub) {
    currentPageBookmarked = false;
    return;
  }

  BookmarkFile::load(epub->getPath(), cachedBookmarks);
  updateBookmarkFlag();
}

void EpubReaderActivity::addBookmark() {
  tiltMessage = false;
  if (!section || !epub) return;
  LOG_DBG("ERS", "Toggle bookmark at spine %d, page %d", currentSpineIndex, section ? section->currentPage : -1);
  int currentPage;
  int pageCount;
  {
    RenderLock lock;
    pageCount = section->estimatedTotalPages();
    currentPage = section->currentPage;
  }

  SavedProgressPosition progress = ProgressMapper::toSavedProgress(epub, getCurrentPosition());
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, currentPage, pageCount);

  const size_t bookmarkCountBeforeToggle = cachedBookmarks.size();
  cachedBookmarks.erase(std::remove_if(cachedBookmarks.begin(), cachedBookmarks.end(),
                                       [&](const BookmarkEntry& b) {
                                         return bookmarkMatchesProgress(b, currentSpineIndex, currentPage, pageCount,
                                                                        pageRange);
                                       }),
                        cachedBookmarks.end());
  if (cachedBookmarks.size() != bookmarkCountBeforeToggle) {
    bookmarkRemoved = true;
    currentPageBookmarked = false;
  } else {
    std::string pageText;
    if (currentPage >= 0 && currentPage < pageCount) {
      pageText = section->getTextFromSectionFile();
    }
    BookmarkEntry entry;
    entry.percentage = progress.percentage;
    entry.xpath = progress.xpath;
    entry.summary = BookmarkUtil::sanitizeBookmarkSummary(pageText);
    entry.computedSpineIndex = currentSpineIndex;
    entry.computedChapterPageCount = pageCount;
    entry.computedChapterProgress = currentPage;
    const std::optional<uint32_t> offset =
        currentPageVisibleOffset.has_value() ? currentPageVisibleOffset
        : (currentPage >= 0 && currentPage < section->pageCount)
            ? section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))
            : std::nullopt;
    if (offset.has_value()) {
      entry.visibleTextOffset = *offset;
      entry.hasVisibleTextOffset = true;
    }
    cachedBookmarks.insert(cachedBookmarks.begin(), entry);
    bookmarkRemoved = false;
    currentPageBookmarked = true;
  }

  if (!BookmarkFile::save(epub->getPath(), cachedBookmarks)) {
    LOG_ERR("ERS", "Failed to save bookmarks");
  }
  requestUpdate();
}

void EpubReaderActivity::updateBookmarkFlag() {
  if (!section || !epub || cachedBookmarks.empty()) {
    currentPageBookmarked = false;
    return;
  }
  const int pageCount = section->estimatedTotalPages();
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, section->currentPage, pageCount);
  currentPageBookmarked = std::any_of(cachedBookmarks.begin(), cachedBookmarks.end(), [&](const BookmarkEntry& b) {
    return bookmarkMatchesProgress(b, currentSpineIndex, section->currentPage, pageCount, pageRange);
  });
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  if (section) {
    info.currentPage = section->currentPage + 1;
    info.totalPages = section->estimatedTotalPages();
    if (epub && !epub->indexComplete()) {
      info.progressPercent = -1;  // unknown: the reading record keeps the book's last one
    } else if (epub && epub->getBookSize() > 0 && info.totalPages > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(info.totalPages);
      int pct = static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      info.progressPercent = pct;
    }
  }
  return info;
}

CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
  localPos.hasResolvedSpineIndex = true;
  localPos.hasMappedPage = true;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    if (const auto offset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))) {
      localPos.visibleTextOffset = *offset;
      localPos.hasVisibleTextOffset = true;
    }
  }
  if (paragraphIndex.has_value()) {
    localPos.paragraphIndex = *paragraphIndex;
    localPos.hasParagraphIndex = true;
  }
  return localPos;
}

void EpubReaderActivity::onPause() {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  tenorchrome::noteReaderFootBar(false, false, -1);
  if (toolbarUi) toolbarUi->closeRouting();
#endif
  pendingManualTurn = 0;
  // The screen that covers the reader draws into the framebuffer.
  pageFrameShown = false;
#ifdef TENOR_TURN_TRACE
  dropTurnTrace(pendingManualTurnTrace, "pause");
#endif
  ReaderActivity::onPause();
}

void EpubReaderActivity::onExit() {
  readertip::close();
  panelClosedLocked(true, false);  // ActivityManager owns RenderLock during exit
  dropCatchUp();  // the next open lays the chapter out from its saved place
#ifdef TENOR_TURN_TRACE
  [[maybe_unused]] const unsigned long exitStarted = millis();
#endif
  // The excerpt below lands on this book's recent entry, so the entry goes in first.
  commitOpen();
  pendingManualTurn = 0;
#ifdef TENOR_TURN_TRACE
  dropTurnTrace(pendingManualTurnTrace, "exit");
  [[maybe_unused]] const unsigned long committed = millis();
  [[maybe_unused]] unsigned long pageLoaded = committed;
#endif
  if (!preview && !recentsEntryRemoved && section && pageReady.load(std::memory_order_acquire)) {
    // One existing page cache read on exit, no EPUB scan and no page-turn overhead.
    auto page = section->loadPage(section->currentPage);
#ifdef TENOR_TURN_TRACE
    pageLoaded = millis();
#endif
    auto excerpt = makeUniqueNoThrow<readingexcerpt::Builder>();
    if (page && excerpt) {
      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageLine) continue;
        const auto* block = static_cast<const PageLine&>(*element).getBlock();
        if (!block || !block->valid()) continue;
        for (uint16_t i = 0; i < block->wordCount(); ++i) excerpt->word(block->wordText(i));
      }
      rememberExcerpt(excerpt->result());
    }
  }
  // A paint cut short by Back left the page it showed unsaved.
  if (progressSaveDeferred.load(std::memory_order_acquire)) saveProgressIfMoved();
#ifdef TENOR_TURN_TRACE
  [[maybe_unused]] const unsigned long excerptDone = millis();
#endif
  ReaderActivity::onExit();
#ifdef TENOR_TURN_TRACE
  [[maybe_unused]] const unsigned long baseDone = millis();
#endif
  // After the base class has handed back the SD font caches.
  writePendingThumbs();
#ifdef TENOR_TURN_TRACE
  LOG_INF("ERS", "EXIT_STAGES excerpt=%lu base=%lu thumbs=%lu commit=%lu load=%lu", excerptDone - exitStarted,
          baseDone - excerptDone, millis() - baseDone, committed - exitStarted, pageLoaded - committed);
#endif
}
