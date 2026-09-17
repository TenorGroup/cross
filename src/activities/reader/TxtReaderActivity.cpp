#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <Epub/ReaderSpacing.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Serialization.h>
#include <Utf8.h>

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderFontChon.h"
#include "ReaderFontSizes.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ReadingExcerpt.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 6;          // Increment when cache format changes
}  // namespace

// Doi co chu roi dung lai chi muc trang, giu dung doan dang doc: trang moi la trang chua
// offset dau trang cu. initializeReader() doc lai font, so dong moi trang va chi muc (cache
// lech font thi tu dung lai).
bool TxtReaderActivity::docCoChuMotNac(const int huong) {
  const std::vector<uint8_t> sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
  if (sizes.empty()) return false;
  const int cur = fontdoc::coDangDung(sizes);
  int moi = cur + huong;
  if (moi < 0) moi = 0;
  if (moi >= static_cast<int>(sizes.size())) moi = static_cast<int>(sizes.size()) - 1;
  if (moi == cur) return false;

  const size_t offsetCu =
      (currentPage >= 0 && currentPage < static_cast<int>(pageOffsets.size())) ? pageOffsets[currentPage] : 0;
  {
    RenderLock lock;
    fontdoc::apCo(renderer, sizes[moi]);
    initialized = false;
    initializeReader(renderer);
    int trang = 0;
    for (size_t i = 0; i < pageOffsets.size(); i++) {
      if (pageOffsets[i] <= offsetCu) trang = static_cast<int>(i);
    }
    currentPage = trang;
    currentPageLines.clear();
  }
  SETTINGS.saveToFile();
  return true;
}

bool TxtReaderActivity::loadBook() {
  txt = makeUniqueNoThrow<Txt>(bookPath, "/.crosspoint");
  if (!txt) {
    LOG_ERR("TRS", "Failed to allocate TXT object");
    return false;
  }
  if (!txt->load()) {
    LOG_ERR("TRS", "Failed to load TXT");
    return false;
  }
  txt->setupCacheDir();
  return true;
}

void TxtReaderActivity::initializeReader(GfxRenderer& renderer) {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  readingMargins(cachedOrientedMarginTop, cachedOrientedMarginRight, cachedOrientedMarginBottom,
                 cachedOrientedMarginLeft);

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = std::max(1, renderer.getLineHeight(cachedFontId, SETTINGS.getReaderLineCompression()));

  cachedLineHeight = lineHeight;
  cachedParagraphGap = readerSpacing::paragraphGap(SETTINGS.extraParagraphSpacing, lineHeight);
  cachedLetterSpacing = readerSpacing::letterPixels(SETTINGS.letterSpacing);
  cachedWordSpacing = SETTINGS.wordSpacing;
  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;
  currentPageLineY.reserve(linesPerPage);
  currentPageLineIndent.reserve(linesPerPage);
  cachedIndent = std::min(viewportWidth / 4,
                          renderer.getSpaceWidth(cachedFontId, EpdFontFamily::REGULAR, cachedWordSpacing) *
                              readerSpacing::indentSpaces(SETTINGS.paragraphIndent));

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  // Try to load cached page index first
  if (!loadPageIndexCache()) {
    // Cache not found, build page index
    buildPageIndex(renderer);
    // Save to cache for next time
    savePageIndexCache();
  }

  // Load saved progress
  if (!preview) loadProgress();

  initialized = true;
}

void TxtReaderActivity::buildPageIndex(GfxRenderer& renderer) {
  pageOffsets.clear();
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(renderer, offset, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
}

bool TxtReaderActivity::loadPageAtOffset(const GfxRenderer& renderer, size_t offset, std::vector<std::string>& outLines,
                                         size_t& nextOffset, std::vector<uint16_t>* lineY,
                                         std::vector<uint16_t>* lineIndent) {
  outLines.clear();
  outLines.reserve(linesPerPage);
  if (lineY) lineY->clear();
  if (lineIndent) lineIndent->clear();
  int indent = 0;
  int y = 0;
  const auto fits = [&]() { return outLines.empty() || y + cachedLineHeight <= viewportHeight; };
  const auto addLine = [&](std::string value) {
    if (lineY) lineY->push_back(static_cast<uint16_t>(y));
    if (lineIndent) lineIndent->push_back(static_cast<uint16_t>(indent));
    outLines.push_back(std::move(value));
    y += cachedLineHeight;
  };
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x01);
  }

  // Parse lines from buffer
  size_t pos = 0;
  uint8_t previous = 0;
  const bool startsParagraph = offset == 0 || (txt->readContent(&previous, offset - 1, 1) && previous == '\n');

  while (pos < chunkSize && fits()) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    size_t lineContentLen = lineEnd - pos;
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);
    size_t lineBytePos = 0;
    const bool naturalAlign = cachedParagraphAlignment == CrossPointSettings::LEFT_ALIGN ||
                              cachedParagraphAlignment == CrossPointSettings::JUSTIFIED;
    const bool paragraphStart = pos > 0 || startsParagraph;

    do {
      indent = naturalAlign && paragraphStart && lineBytePos == 0 && !line.empty() ? cachedIndent : 0;
      const int lineWidthLimit = viewportWidth - indent;
      if (line.empty()) {
        addLine({});
        break;
      }

      // Grow one screen line from the start. Walking backwards from the end of
      // a long paragraph repeatedly measured nearly the entire paragraph.
      // Temporary termination avoids allocating a substring for each width check.
      const auto prefixFits = [&](size_t length) {
        const char saved = line[length];
        line[length] = '\0';
        const int width =
            renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR, cachedLetterSpacing,
                                                       cachedWordSpacing);
        line[length] = saved;
        return width <= lineWidthLimit;
      };
      size_t breakPos = 0;
      size_t candidate = line.find(' ', 1);
      while (true) {
        if (candidate == std::string::npos) candidate = line.size();
        if (!prefixFits(candidate)) break;
        breakPos = candidate;
        if (candidate == line.size()) break;
        candidate = line.find(' ', candidate + 1);
      }
      if (breakPos == line.size()) {
        addLine(line);
        lineBytePos = displayLen;
        line.clear();
        break;
      }
      if (breakPos == 0) {
        // A word wider than the viewport is split only at UTF-8 boundaries.
        size_t next = 0;
        while (next < line.size()) {
          ++next;
          while (next < line.size() && (line[next] & 0xC0) == 0x80) ++next;
          if (!prefixFits(next)) {
            if (breakPos == 0) breakPos = next;
            break;
          }
          breakPos = next;
        }
      }

      addLine(line.substr(0, breakPos));

      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    } while (!line.empty() && fits());

    if (line.empty()) {
      if (displayLen > 0) y += cachedParagraphGap;
      pos = lineEnd + (lineEnd < chunkSize && buffer[lineEnd] == '\n' ? 1 : 0);
    } else {
      pos = pos + lineBytePos;
      break;
    }
  }

  if (pos == 0 && !outLines.empty()) {
    pos = 1;
  }

  nextOffset = offset + pos;
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);
  return !outLines.empty();
}

void TxtReaderActivity::renderBook() {
  if (!txt) {
    return;
  }

  if (!initialized) {
    initializeReader(renderer);
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(renderer, offset, currentPageLines, nextOffset, &currentPageLineY, &currentPageLineIndent);

  renderer.clearScreen();
  renderPage(renderer);

  // Save progress
  saveProgress();
}

void TxtReaderActivity::renderPage(GfxRenderer& renderer) {
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    size_t row = 0;
    for (const auto& line : currentPageLines) {
      const int indent = currentPageLineIndent[row];
      const int y = cachedOrientedMarginTop + currentPageLineY[row++];
      if (!line.empty()) {
        int x = cachedOrientedMarginLeft;
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        const int textWidth =
            renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR, cachedLetterSpacing,
                                                       cachedWordSpacing);

        // Apply text alignment
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            x = cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            break;
        }

        x += lineIsRtl ? -indent : indent;
        renderer.drawText(cachedFontId, x, y, line.c_str(), true, EpdFontFamily::REGULAR, BidiUtils::BidiBaseDir::AUTO,
                          cachedLetterSpacing, cachedWordSpacing);
      }
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();      // scan pass
  renderStatusBar();  // scan: a CJK title joins the batch prewarm
  scope.endScanAndPrewarm();

  // BW rendering
  renderLines();
  renderStatusBar();

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh);
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
}

void TxtReaderActivity::renderStatusBar() const {
  if (preview) {
    drawPreviewFooter();
    return;
  }
  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  std::string title;
  if (SETTINGS.statusBarSpec().showsTitle()) {
    title = txt->getTitle();
  }
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title);
}

bool TxtReaderActivity::latTrangThat(bool isForward) {
  // Ignore paging until initializeReader has established the page index
  if (!initialized) {
    return false;
  }
  if (isForward) {
    if (currentPage < totalPages) {
      currentPage++;
      return true;
    }
  } else {
    if (currentPage > 0) {
      currentPage--;
      return true;
    }
  }
  return false;
}

bool TxtReaderActivity::skipPages(int amount) {
  if (!initialized) {
    return false;
  }
  int newPage = currentPage + amount;
  if (newPage < 0) newPage = 0;
  // Clamp to totalPages, not totalPages - 1: pageTurn() lets currentPage reach
  // totalPages and isAtEndOfBook() treats that as the end-of-book sentinel, so
  // a forward skip must be able to reach it too.
  if (newPage > totalPages) newPage = totalPages;
  if (newPage != currentPage) {
    currentPage = newPage;
    return true;
  }
  return false;
}

bool TxtReaderActivity::isAtEndOfBook() const { return initialized && currentPage >= totalPages; }

void TxtReaderActivity::onReturnFromEndOfBook() { currentPage = totalPages > 0 ? totalPages - 1 : 0; }

void TxtReaderActivity::saveProgress() const {
  if (preview) return;
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = 0;
  data[3] = 0;
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data))) {
    LOG_ERR("TRS", "Failed to save progress: page %d", currentPage);
  }
}

void TxtReaderActivity::loadProgress() {
  HalFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
      if (currentPage >= totalPages) {
        currentPage = totalPages - 1;
      }
      if (currentPage < 0) {
        currentPage = 0;
      }
      LOG_DBG("TRS", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
  }
}

bool TxtReaderActivity::loadPageIndexCache() {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    return false;
  }

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  int32_t height, lineHeight, paragraphGap;
  int8_t spacing;
  uint8_t wordSpacing;
  serialization::readPod(f, height);
  serialization::readPod(f, lineHeight);
  serialization::readPod(f, paragraphGap);
  serialization::readPod(f, spacing);
  serialization::readPod(f, wordSpacing);
  uint16_t indent;
  serialization::readPod(f, indent);
  if (indent != cachedIndent) return false;
  if (height != viewportHeight || lineHeight != cachedLineHeight || paragraphGap != cachedParagraphGap ||
      spacing != cachedLetterSpacing || wordSpacing != cachedWordSpacing)
    return false;

  uint32_t numPages;
  serialization::readPod(f, numPages);

  pageOffsets.clear();
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void TxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<int32_t>(viewportHeight));
  serialization::writePod(f, static_cast<int32_t>(cachedLineHeight));
  serialization::writePod(f, static_cast<int32_t>(cachedParagraphGap));
  serialization::writePod(f, cachedLetterSpacing);
  serialization::writePod(f, cachedWordSpacing);
  serialization::writePod(f, cachedIndent);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = totalPages > 0 ? static_cast<int>((currentPage + 1) * 100.0f / totalPages + 0.5f) : 0;
  if (info.progressPercent > 100) info.progressPercent = 100;
  return info;
}

void TxtReaderActivity::onExit() {
  if (!preview && pageReady.load(std::memory_order_acquire)) {
    auto excerpt = makeUniqueNoThrow<readingexcerpt::Builder>();
    if (excerpt) {
      for (const auto& line : currentPageLines) excerpt->line(line);
      RECENT_BOOKS.rememberExcerpt(bookPath, excerpt->result());
    }
  }
  ReaderActivity::onExit();
}
