#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <Epub/ReaderSpacing.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <RecoverableFile.h>
#include <Serialization.h>
#include <Utf8.h>

#include <climits>
#include <limits>

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderFontChon.h"
#include "ReaderFontSizes.h"
#include "ReaderToolbarUi.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ReadingExcerpt.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 8;          // v7 could persist an incomplete index after an I/O failure
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
      (currentPage >= 0 && currentPage < static_cast<int>(pageOffsetCount)) ? pageOffsets[currentPage] : 0;
  {
    RenderLock lock;
    fontdoc::apCo(renderer, sizes[moi]);
    initialized = false;
    initializeReader(renderer);
    int trang = 0;
    for (size_t i = 0; i < pageOffsetCount; i++) {
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
  cachedInkHeight = renderer.getFontAscenderSize(cachedFontId) + renderer.getFontDescenderSize(cachedFontId);
  cachedParagraphGap = readerSpacing::paragraphGap(SETTINGS.extraParagraphSpacing, lineHeight);
  cachedLetterSpacing = readerSpacing::letterPixels(SETTINGS.letterSpacing);
  cachedWordSpacing = SETTINGS.wordSpacing;
  // So dong toi da mot trang theo luat muc: dong cuoi chi can muc (cachedInkHeight) nam trong khung.
  linesPerPage = viewportHeight >= cachedInkHeight ? (viewportHeight - cachedInkHeight) / lineHeight + 1 : 1;
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
    if (!buildPageIndex(renderer)) {
      LOG_ERR("TRS", "Page index incomplete; leaving reader uninitialized");
      return;
    }
    // A completed in-memory index remains usable if only cache persistence fails.
    if (!savePageIndexCache()) LOG_ERR("TRS", "Could not persist page index");
  }

  // Load saved progress
  if (!preview) loadProgress();

  initialized = true;
}

bool TxtReaderActivity::reservePageOffsets(const size_t count) {
  if (count <= pageOffsetCapacity) return true;
  if (count > static_cast<size_t>(INT_MAX) || count > std::numeric_limits<size_t>::max() / sizeof(uint32_t)) return false;
  auto offsets = makeUniqueNoThrow<uint32_t[]>(count);
  if (!offsets) return false;
  if (pageOffsetCount > 0) std::copy_n(pageOffsets.get(), pageOffsetCount, offsets.get());
  pageOffsets = std::move(offsets);
  pageOffsetCapacity = count;
  return true;
}

bool TxtReaderActivity::addPageOffset(const size_t offset) {
  if (offset > std::numeric_limits<uint32_t>::max()) return false;
  if (pageOffsetCount == pageOffsetCapacity) {
    const size_t capacity = pageOffsetCapacity == 0 ? 1 : pageOffsetCapacity * 2;
    if (capacity < pageOffsetCapacity || !reservePageOffsets(capacity)) return false;
  }
  pageOffsets[pageOffsetCount++] = static_cast<uint32_t>(offset);
  return true;
}

bool TxtReaderActivity::buildPageIndex(GfxRenderer& renderer) {
  pageOffsetCount = 0;
  totalPages = 0;
  const size_t fileSize = txt->getFileSize();
  if (fileSize > std::numeric_limits<uint32_t>::max() || !addPageOffset(0)) return false;

  size_t offset = 0;
  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);
  GUI.drawPopup(renderer, readerugly::notice(StrId::STR_INDEXING));

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;
    if (!loadPageAtOffset(renderer, offset, tempLines, nextOffset) || nextOffset <= offset || nextOffset > fileSize) {
      pageOffsetCount = 0;
      return false;
    }
    offset = nextOffset;
    if (offset < fileSize && !addPageOffset(offset)) {
      pageOffsetCount = 0;
      return false;
    }
    if (pageOffsetCount % 20 == 0) vTaskDelay(1);
  }

  totalPages = static_cast<int>(pageOffsetCount);
  LOG_DBG("TRS", "Built complete page index: %d pages", totalPages);
  return true;
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
  const auto fits = [&]() { return outLines.empty() || y + cachedInkHeight <= viewportHeight; };
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

  uint8_t previous = 0;
  if (offset > 0 && !txt->readContent(&previous, offset - 1, 1)) {
    free(buffer);
    return false;
  }
  const bool startsParagraph = offset == 0 || previous == '\n';

  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x01);
  }

  // Parse lines from buffer
  size_t pos = 0;

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

  if (!initialized) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (txt->getFileSize() == 0 || pageOffsetCount == 0) {
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
  if (!loadPageAtOffset(renderer, offset, currentPageLines, nextOffset, &currentPageLineY, &currentPageLineIndent)) {
    LOG_ERR("TRS", "Failed to load page at offset %zu", offset);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen();
  renderPage(renderer);
  markPageRendered();

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
  const bool linkNote = SETTINGS.statusBarSpec().showsTitle() && linkNoteTitle(title);
  if (SETTINGS.statusBarSpec().showsTitle() && !linkNote) {
    title = txt->getTitle();
  }
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title, 0, 0, true, false, false, !linkNote);
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

std::string TxtReaderActivity::pageIndexCachePath() const {
  return txt->getCachePath() + (preview ? "/preview_index.bin" : "/index.bin");
}

bool TxtReaderActivity::loadPageIndexCache() {
  const std::string path = pageIndexCachePath();
  if (!freeink::recoverFile(Storage, path.c_str())) return false;
  const std::string backup = path + ".davbak";
  if (loadPageIndexCacheFile(path)) {
    // A fully validated canonical cache supersedes any backup left after commit.
    if (Storage.exists(backup.c_str())) Storage.remove(backup.c_str());
    return true;
  }
  return loadPageIndexCacheFile(backup);
}

bool TxtReaderActivity::loadPageIndexCacheFile(const std::string& path) {
  HalFile f;
  if (!Storage.openFileForRead("TRS", path, f)) return false;
  serialization::CheckedReader reader(f);
  uint32_t magic = 0, fileSize = 0, numPages = 0;
  uint8_t version = 0, alignment = 0, wordSpacing = 0;
  int32_t width = 0, lines = 0, fontId = 0, margin = 0, height = 0, lineHeight = 0, paragraphGap = 0;
  int8_t spacing = 0;
  uint16_t indent = 0;
  if (!reader.pod(magic) || magic != CACHE_MAGIC || !reader.pod(version) || version != CACHE_VERSION ||
      !reader.pod(fileSize) || fileSize != txt->getFileSize() || !reader.pod(width) || width != viewportWidth ||
      !reader.pod(lines) || lines != linesPerPage || !reader.pod(fontId) || fontId != cachedFontId ||
      !reader.pod(margin) || margin != cachedScreenMargin || !reader.pod(alignment) ||
      alignment != cachedParagraphAlignment || !reader.pod(height) || height != viewportHeight ||
      !reader.pod(lineHeight) || lineHeight != cachedLineHeight || !reader.pod(paragraphGap) ||
      paragraphGap != cachedParagraphGap || !reader.pod(spacing) || spacing != cachedLetterSpacing ||
      !reader.pod(wordSpacing) || wordSpacing != cachedWordSpacing || !reader.pod(indent) || indent != cachedIndent ||
      !reader.pod(numPages)) return false;

  // Bound the complete table using both the cache payload and the source. Validate
  // every offset before requesting any allocation from untrusted cache metadata.
  if (numPages == 0 || numPages > static_cast<uint32_t>(INT_MAX) ||
      numPages > std::max<uint32_t>(1, fileSize) || reader.remaining() % sizeof(uint32_t) != 0 ||
      numPages != reader.remaining() / sizeof(uint32_t)) return false;
  const size_t offsetsStart = reader.position();
  uint32_t previous = 0;
  for (uint32_t i = 0; i < numPages; ++i) {
    uint32_t offset = 0;
    if (!reader.pod(offset) || (i == 0 ? offset != 0 : offset <= previous) ||
        (fileSize == 0 ? offset != 0 : offset >= fileSize)) return false;
    previous = offset;
  }

  pageOffsetCount = 0;
  if (!reader.seek(offsetsStart) || !reservePageOffsets(numPages)) return false;
  previous = 0;
  for (uint32_t i = 0; i < numPages; ++i) {
    uint32_t offset = 0;
    if (!reader.pod(offset) || (i == 0 ? offset != 0 : offset <= previous) ||
        (fileSize == 0 ? offset != 0 : offset >= fileSize)) return false;
    pageOffsets[i] = offset;
    previous = offset;
  }
  pageOffsetCount = numPages;
  totalPages = static_cast<int>(numPages);
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

bool TxtReaderActivity::savePageIndexCache() const {
  if (pageOffsetCount == 0 || totalPages != static_cast<int>(pageOffsetCount)) return false;
  const std::string cachePath = pageIndexCachePath();
  const std::string staging = cachePath + ".tmp";
  HalFile f;
  if (!Storage.openFileForWrite("TRS", staging, f)) return false;

  bool ok = serialization::writePod(f, CACHE_MAGIC) && serialization::writePod(f, CACHE_VERSION) &&
            serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize())) &&
            serialization::writePod(f, static_cast<int32_t>(viewportWidth)) &&
            serialization::writePod(f, static_cast<int32_t>(linesPerPage)) &&
            serialization::writePod(f, static_cast<int32_t>(cachedFontId)) &&
            serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin)) &&
            serialization::writePod(f, cachedParagraphAlignment) &&
            serialization::writePod(f, static_cast<int32_t>(viewportHeight)) &&
            serialization::writePod(f, static_cast<int32_t>(cachedLineHeight)) &&
            serialization::writePod(f, static_cast<int32_t>(cachedParagraphGap)) &&
            serialization::writePod(f, cachedLetterSpacing) && serialization::writePod(f, cachedWordSpacing) &&
            serialization::writePod(f, cachedIndent) &&
            serialization::writePod(f, static_cast<uint32_t>(pageOffsetCount));
  for (size_t i = 0; ok && i < pageOffsetCount; ++i) {
    ok = serialization::writePod(f, pageOffsets[i]);
  }
  ok = ok && f.sync();
  const bool closed = f.close();
  if (!ok || !closed) {
    Storage.remove(staging.c_str());
    return false;
  }

  bool backupCleanupPending = false;
  if (!freeink::recoverFile(Storage, cachePath.c_str()) ||
      !freeink::replaceFile(Storage, staging.c_str(), cachePath.c_str(), &backupCleanupPending)) {
    Storage.remove(staging.c_str());
    return false;
  }
  if (backupCleanupPending) LOG_ERR("TRS", "Page index saved; backup cleanup pending");
  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
  return true;
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
      rememberExcerpt(excerpt->result());
    }
  }
  ReaderActivity::onExit();
}
