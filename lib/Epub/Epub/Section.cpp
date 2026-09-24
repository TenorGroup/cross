#include "Section.h"

#include <Arduino.h>
#include <HalStorage.h>

#include <algorithm>
#include <Logging.h>
#include <Memory.h>
#include <RecoverableFile.h>
#include <Serialization.h>

#include "Epub/css/CssParser.h"
#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// v28: text decoration bits now include line-through in serialized wordStyles.
// v29: TextBlock word data stored as one flat arena (offset table + NUL-terminated
// text blob) instead of length-prefixed strings and per-field arrays.
// v30: Arabic shaping changed both drawing and measurement (getTextAdvanceX now
//      measures the shaped visual text); cached word positions from v29 no longer
//      match what drawText renders.
// v32: ImageBlock serializes the book-internal source href after the cache path
//      (lazy extraction: images are header-probed at build time and extracted on
//      first render).
// v33: Support <ruby> and <rt> tags. Skip <rp> tags
// v34: Word gaps are only suppressed for tokens glued in the source, so spaces between
//      Hangul words survive again; ruby element boundaries carry the continuation flag
//      instead. Invalidates v33 caches, whose word positions have the spaces collapsed.

// v34: <br> handling changed layout - a <br> after text is now a margin-stripped
//      line break (browser-like) and only a <br> whose block stays empty injects
//      the scene-break gap, so cached pages laid out by older versions no longer
//      match. Keeps <br>-per-paragraph books (common CJK formatting) from
//      re-adding container spacing at every paragraph.
// v35: Persist a uint32_t visible-text start offset for every page.
// v36: Ruby and CJK justification layout changes invalidate cached word positions.
// v37: Footnote href records grew from 96 to 256 bytes.
// v38: Focus Reading line breaking changed - a visible hyphen/dash inside a word is now a
//      break opportunity, and hyphenation of a focus-split word considers the whole word
//      instead of only its regular-weight suffix. Pages cached by older versions were laid
//      out with the previous, more restrictive break set and no longer match.
// v39: Image top margin is clamped so a full-viewport-height image cannot
//      overflow the page bottom; older caches can hold placements that panels
//      with no bottom inset refuse to draw.
// v40: Ruby groups remain intact when a large text block is soft-flushed.
// v41: Simple HTML table rows are laid out as positioned columns instead of
//      flattened paragraphs with synthetic row/cell labels.
// v42: Closing a block strips inherited vertical margins and padding.
// v43: Paragraph base direction excludes direction changes from inline elements.
// v44: Persist internal-link rectangles with each page for touch navigation.
// v45: Internal EPUB links preserve CSS superscript/subscript positioning.
// v46: Independent first-line indentation mode in the render-spec cache key.
// v51: The drop cap boolean became a three-value mode whose size differs, so a
// v50 header would be read as a different setting; those caches are discarded.
// v53: Discard caches whose older builders could silently omit failed lines.
// v54: Rebuild pages whose CSS cascade could be skipped under low heap.
constexpr uint8_t SECTION_FILE_VERSION = 54;
// Written into the version field while a build is in progress; patched to
// SECTION_FILE_VERSION only when the build is finalized. An abandoned /
// crash-interrupted .bin therefore carries version 0, which loadSectionFile rejects
// as unknown and clears -- so an incomplete file is never mistaken for a valid one.
constexpr uint8_t SECTION_FILE_INCOMPLETE_VERSION = 0;
// Written when a build is suspended partway (reader exited or device slept mid-build).
// The file carries valid pages 0..pageCount-1, all LUTs, and a trailer with the parse
// watermark (bytesConsumed, totalBytes) appended after the li LUT. loadSectionFile
// accepts it so a resume shows those pages instantly; the reader extends it by
// rebuilding in the background. Uses the same header layout as SECTION_FILE_VERSION,
// so finalized files are untouched by this feature; older firmware treats the sentinel
// as an unknown version and rebuilds, which is a safe downgrade.
// MUST change in lockstep with SECTION_FILE_VERSION: the sentinel IS the partial's
// format version, so a stale-format partial otherwise passes the header check and
// only fails (noisily, via the block-decode error path) when a page is loaded.
// Derived so the pairing can't be forgotten: 0xFE for v28, 0xFD for v29, ...
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION = 0xFE - (SECTION_FILE_VERSION - 28);
constexpr uint32_t HEADER_SIZE =
    sizeof(int8_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
    sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
    sizeof(uint8_t) + sizeof(bool) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) +
    sizeof(uint32_t);
}  // namespace

// Out-of-line so the unique_ptr<ChapterHtmlSlimParser> in BuildContext can be
// constructed/destroyed where the parser's full definition is visible.
Section::Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer, const bool preview)
    : epub(epub),
      spineIndex(spineIndex),
      renderer(renderer),
      filePath(epub->getCachePath() + (preview ? "/sections/preview_" : "/sections/") + std::to_string(spineIndex) +
               ".bin"),
      preview_(preview) {}

// Suspend any in-progress build so every section.reset() / navigation / sleep path
// persists the pages already laid out as a partial .bin instead of discarding them
// (no-op once a build has completed or never started).
Section::~Section() { suspendBuild(); }

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file || !page) {
    LOG_ERR("SCT", "File not open for writing page %d", builtPageCount_);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("SCT", "Failed to serialize page %d", builtPageCount_);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", builtPageCount_);

  return position;
}

bool Section::writeSectionFileHeader(const ReaderRenderSpec& spec) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return false;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(spec.fontId) + sizeof(spec.lineCompression) +
                                   sizeof(spec.extraParagraphSpacing) + sizeof(spec.paragraphIndent) +
                                   sizeof(spec.letterSpacing) + sizeof(spec.wordSpacing) +
                                   sizeof(spec.paragraphAlignment) +
                                   sizeof(spec.viewportWidth) + sizeof(spec.viewportHeight) + sizeof(pageCount) +
                                   sizeof(spec.hyphenationEnabled) + sizeof(spec.embeddedStyle) +
                                   sizeof(spec.imageRendering) + sizeof(spec.dropCapMode) + sizeof(uint32_t) +
                                   sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t),
                "Header size mismatch");
  // Written as the incomplete sentinel; finalizeBuild() patches it to
  // SECTION_FILE_VERSION as the last step, committing the file.
  if (!serialization::writePod(file, SECTION_FILE_INCOMPLETE_VERSION)) return false;
  if (!serialization::writePod(file, spec.fontId)) return false;
  if (!serialization::writePod(file, spec.lineCompression)) return false;
  if (!serialization::writePod(file, spec.extraParagraphSpacing)) return false;
  if (!serialization::writePod(file, spec.paragraphIndent)) return false;
  if (!serialization::writePod(file, spec.letterSpacing)) return false;
  if (!serialization::writePod(file, spec.wordSpacing)) return false;
  if (!serialization::writePod(file, spec.paragraphAlignment)) return false;
  if (!serialization::writePod(file, spec.viewportWidth)) return false;
  if (!serialization::writePod(file, spec.viewportHeight)) return false;
  if (!serialization::writePod(file, spec.hyphenationEnabled)) return false;
  if (!serialization::writePod(file, spec.embeddedStyle)) return false;
  if (!serialization::writePod(file, spec.imageRendering)) return false;
  if (!serialization::writePod(file, spec.dropCapMode)) return false;
  if (!serialization::writePod(file, pageCount)) return false;  // Placeholder for page count (will be initially 0, patched later)
  if (!serialization::writePod(file, static_cast<uint32_t>(0))) return false;  // Placeholder for LUT offset (patched later)
  if (!serialization::writePod(file, static_cast<uint32_t>(0))) return false;  // Placeholder for anchor map offset (patched later)
  if (!serialization::writePod(file, static_cast<uint32_t>(0))) return false;  // Placeholder for paragraph LUT offset (patched later)
  if (!serialization::writePod(file, static_cast<uint32_t>(0))) return false;  // Placeholder for li LUT offset (patched later)
  if (!serialization::writePod(file, static_cast<uint32_t>(0))) return false;  // Placeholder for visible-offset LUT (patched later)
  return true;
}

bool Section::loadSectionFile(const ReaderRenderSpec& spec) {
  if (!freeink::recoverFile(Storage, filePath.c_str())) return false;
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  const auto invalid = [this]() {
    file.close();
    clearCache();
    pageCount = 0;
    partial_ = false;
    partialPageCount_ = 0;
    return false;
  };
  serialization::CheckedReader reader(file);
  if (!reader.has(HEADER_SIZE)) return invalid();
  // Match parameters
  bool filePartial = false;
  {
    uint8_t version;
    if (!reader.pod(version)) return invalid();
    if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }
    filePartial = (version == SECTION_FILE_PARTIAL_VERSION);

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    uint8_t fileExtraParagraphSpacing;
    uint8_t fileParagraphIndent;
    int8_t fileLetterSpacing;
    uint8_t fileWordSpacing;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileEmbeddedStyle;
    uint8_t fileImageRendering;
    uint8_t fileDropCapMode;
    if (!reader.pod(fileFontId)) return invalid();
    if (!reader.pod(fileLineCompression)) return invalid();
    if (!reader.pod(fileExtraParagraphSpacing)) return invalid();
    if (!reader.pod(fileParagraphIndent)) return invalid();
    if (!reader.pod(fileLetterSpacing)) return invalid();
    if (!reader.pod(fileWordSpacing)) return invalid();
    if (!reader.pod(fileParagraphAlignment)) return invalid();
    if (!reader.pod(fileViewportWidth)) return invalid();
    if (!reader.pod(fileViewportHeight)) return invalid();
    if (!reader.pod(fileHyphenationEnabled)) return invalid();
    if (!reader.pod(fileEmbeddedStyle)) return invalid();
    if (!reader.pod(fileImageRendering)) return invalid();
    if (!reader.pod(fileDropCapMode)) return invalid();

    if (spec.fontId != fileFontId || spec.lineCompression != fileLineCompression ||
        spec.extraParagraphSpacing != fileExtraParagraphSpacing || spec.paragraphIndent != fileParagraphIndent ||
        spec.letterSpacing != fileLetterSpacing || spec.wordSpacing != fileWordSpacing ||
        spec.paragraphAlignment != fileParagraphAlignment ||
        spec.viewportWidth != fileViewportWidth || spec.viewportHeight != fileViewportHeight ||
        spec.hyphenationEnabled != fileHyphenationEnabled || spec.embeddedStyle != fileEmbeddedStyle ||
        spec.imageRendering != fileImageRendering || spec.dropCapMode != fileDropCapMode) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      LOG_INF("SCT", "Section cache MISS: params %ux%u font %d != file %ux%u font %d",
              static_cast<unsigned>(spec.viewportWidth), static_cast<unsigned>(spec.viewportHeight), spec.fontId,
              static_cast<unsigned>(fileViewportWidth), static_cast<unsigned>(fileViewportHeight), fileFontId);
      clearCache();
      return false;
    }
  }

  if (!reader.pod(pageCount)) return invalid();
  uint32_t pageLut = 0, anchors = 0, paragraphs = 0, items = 0, visible = 0;
  if (!reader.pod(pageLut) || !reader.pod(anchors) || !reader.pod(paragraphs) ||
      !reader.pod(items) || !reader.pod(visible)) return invalid();
  const uint64_t count = pageCount;
  if (pageCount == 0 || pageLut < HEADER_SIZE || anchors != pageLut + count * sizeof(uint32_t) ||
      paragraphs < anchors + sizeof(uint16_t) || items != paragraphs + sizeof(uint16_t) + count * sizeof(uint16_t) ||
      visible != items + count * sizeof(uint16_t) || visible + count * sizeof(uint32_t) > file.size()) return invalid();
  if (!reader.seek(pageLut)) return invalid();
  uint32_t previous = 0;
  for (uint16_t i = 0; i < pageCount; ++i) {
    uint32_t offset = 0;
    if (!reader.pod(offset) || offset < HEADER_SIZE || offset >= pageLut || (i > 0 && offset <= previous))
      return invalid();
    previous = offset;
  }

  // Dau vet truc tiep cho nghiem thu: mot dong cho biet cache duoc DUNG LAI hay khong,
  // cung voi vung nhin da ghi trong header. Khong doi hanh vi, chi de doc.
  LOG_INF("SCT", "Section cache HIT: %u pages, viewport %ux%u, partial=%u", static_cast<unsigned>(pageCount),
          static_cast<unsigned>(spec.viewportWidth), static_cast<unsigned>(spec.viewportHeight),
          static_cast<unsigned>(filePartial ? 1 : 0));

  if (filePartial) {
    // A partial's pageCount is the watermark of a suspended build. Read the watermark
    // trailer (appended after the visible-offset LUT) so estimatedTotalPages can extrapolate.
    uint32_t liLutOffset = 0;
    if (!reader.seek(HEADER_SIZE - sizeof(uint32_t) * 2)) return invalid();
    if (!reader.pod(liLutOffset)) return invalid();
    uint32_t visibleLutOffset = 0;
    if (!reader.seek(HEADER_SIZE - sizeof(uint32_t))) return invalid();
    if (!reader.pod(visibleLutOffset)) return invalid();
    const uint64_t trailerOffset = static_cast<uint64_t>(visibleLutOffset) + static_cast<uint32_t>(pageCount) * sizeof(uint32_t);
    const bool trailerValid = pageCount > 0 && liLutOffset >= HEADER_SIZE && visibleLutOffset > liLutOffset &&
                              trailerOffset + 2 * sizeof(uint32_t) <= file.size();
    if (!trailerValid) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: malformed partial section");
      clearCache();
      pageCount = 0;
      return false;
    }
    if (!reader.seek(trailerOffset)) return invalid();
    if (!reader.pod(partialBytesConsumed_)) return invalid();
    if (!reader.pod(partialTotalBytes_)) return invalid();
    partial_ = true;
    partialPageCount_ = pageCount;
  }

  // Reconcile a completed promotion only after the canonical header and LUTs
  // validate. A leftover backup otherwise blocks every later partial extension.
  if (!file.close()) return false;
  const std::string backup = filePath + ".davbak";
  if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) {
    LOG_DBG("SCT", "Validated section retains recovery backup; cleanup will retry on load");
  }
  LOG_DBG("SCT", "Deserialization succeeded: %d pages%s", pageCount, filePartial ? " (partial)" : "");
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() const {
  Storage.remove(lutTmpPath().c_str());
  Storage.remove(checkpointTmpPath().c_str());
  Storage.remove((filePath + ".davbak").c_str());
  const std::string tmpBin = binTmpPath();
  if (Storage.exists(tmpBin.c_str())) {
    Storage.remove(tmpBin.c_str());
  }
  if (!Storage.exists(filePath.c_str())) {
    LOG_DBG("SCT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("SCT", "Cache cleared successfully");
  return true;
}

bool Section::createSectionFile(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  // One-shot build: start, then lay out the whole section in a single pass.
  if (!startBuild(spec, popupFn)) {
    return false;
  }
  if (!buildSomeMore(0)) {  // 0 = build to completion
    return false;
  }
  return buildComplete_;
}

bool Section::loadBuildCss(BuildContext* ctx) {
  const auto& spec = ctx->spec;
  if (spec.embeddedStyle) {
    ctx->cssParser = epub->getCssParser();
    if (ctx->cssParser) {
#ifdef TENOR_UI_ACCEPTANCE
      LOG_DBG("SCT", "EPUB_BUILD stage=css_before free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
      const CssParser::CacheLoadResult cacheResult = ctx->cssParser->loadFromCache();
#ifdef TENOR_UI_ACCEPTANCE
      LOG_DBG("SCT", "EPUB_BUILD stage=css_after result=%u free=%u largest=%u", static_cast<unsigned>(cacheResult),
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
      if (cacheResult == CssParser::CacheLoadResult::LowMemory) {
        LOG_ERR("SCT", "Insufficient heap to hydrate CSS; section build deferred free=%u largest=%u",
                static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
        ctx->cssParser->clear();
        return false;
      }
      if (cacheResult == CssParser::CacheLoadResult::Invalid) {
        LOG_ERR("SCT", "Failed to load CSS from cache free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
      }
    }
  } else {
#ifdef TENOR_UI_ACCEPTANCE
    LOG_DBG("SCT", "EPUB_BUILD stage=css_disabled free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  }

  return true;
}

std::unique_ptr<ChapterHtmlSlimParser> Section::makeBuildParser(BuildContext* ctxPtr, const ReaderRenderSpec& spec,
                                                                const std::function<void()>& popupFn) {
  // Collect TOC anchors for this spine so the parser can insert page breaks at chapter boundaries.
  // A novel shipped as one XHTML file can carry thousands: each is a heap string the parser keeps
  // and scans for every id, so only the spine's first MAX_TOC_ANCHORS break pages. The cut is by
  // TOC position, never by build progress, so every build of a spine paginates the same way; later
  // anchors are still recorded under the parser's ordinary id budget.
  constexpr size_t MAX_TOC_ANCHORS = 256;
  std::vector<std::string> tocAnchors;
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex >= 0) {
    for (int i = startTocIndex; i < epub->getTocItemsCount() && tocAnchors.size() < MAX_TOC_ANCHORS; i++) {
      auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spineIndex) break;
      if (!entry.anchor.empty()) {
        tocAnchors.push_back(std::move(entry.anchor));
      }
    }
  }

  // The parser stores the path/contentBase/imageBasePath by reference, so they must
  // live in the BuildContext (which outlives the parser). The page-complete callback
  // captures the BuildContext pointer to append to its on-disk LUT; build_ owns the
  // context for the parser's whole lifetime.
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("SCT", "EPUB_BUILD stage=parser_before free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  return makeUniqueNoThrow<ChapterHtmlSlimParser>(
      epub, ctxPtr->parsePath, renderer, spec.fontId, spec.lineCompression, spec.extraParagraphSpacing,
      spec.paragraphAlignment, spec.viewportWidth, spec.viewportHeight, spec.hyphenationEnabled, spec.dropCapMode,
      [this, ctxPtr](std::unique_ptr<Page> page, const uint16_t paragraphIndex, const uint16_t listItemIndex,
                     const uint32_t visibleTextOffset) {
        if (ctxPtr->failed) return;
        // The serialized page count is uint16_t. Refuse overflow explicitly.
        if (builtPageCount_ == UINT16_MAX) {
          ctxPtr->failed = true;
          ctxPtr->parser->failBuild();
          return;
        }
        const uint32_t position = this->onPageComplete(std::move(page));
        const PageLutEntry entry{position, paragraphIndex, listItemIndex, visibleTextOffset};
        if (position == 0 || !ctxPtr->lut.seek(static_cast<size_t>(builtPageCount_) * sizeof(entry)) ||
            ctxPtr->lut.write(&entry, sizeof(entry)) != sizeof(entry)) {
          ctxPtr->failed = true;
          ctxPtr->parser->failBuild();
          return;
        }
#ifdef TENOR_UI_ACCEPTANCE
        if (builtPageCount_ == 0) {
          LOG_DBG("SCT", "EPUB_PAGE_FIRST spine=%d visible=%u file_offset=%u free=%u largest=%u", spineIndex,
                  static_cast<unsigned>(visibleTextOffset), static_cast<unsigned>(position),
                  static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
        }
#endif
        ++builtPageCount_;
        pageCount = std::max(pageCount, builtPageCount_);
        ctxPtr->lastVisibleTextOffset = visibleTextOffset;
      },
      spec.embeddedStyle, ctxPtr->contentBase, ctxPtr->imageBasePath, spec.imageRendering, tocAnchors, popupFn,
      ctxPtr->cssParser, spec.paragraphIndent, spec.letterSpacing, spec.wordSpacing);
}

bool Section::startBuild(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  if (build_) {
    LOG_ERR("SCT", "startBuild called while a build is already active");
    return false;
  }
  buildComplete_ = false;
  builtPageCount_ = 0;
  if (!freeink::recoverFile(Storage, filePath.c_str())) return false;
  Storage.remove(lutTmpPath().c_str());
  Storage.remove(checkpointTmpPath().c_str());
  // Pages from a loaded partial stay readable (from filePath) while this build writes
  // to the tmp .bin, so availability never drops below the partial's watermark.
  pageCount = partial_ ? partialPageCount_ : 0;

  // Remove a stale tmp .bin from a crash-interrupted build; this build recreates it.
  {
    const std::string staleTmp = binTmpPath();
    if (Storage.exists(staleTmp.c_str())) {
      Storage.remove(staleTmp.c_str());
    }
  }

  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto htmlDir = epub->getCachePath() + "/html";
  const auto htmlPath = htmlDir + "/" + std::to_string(spineIndex) + ".html";
  const auto tmpHtmlPath = htmlDir + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directory if it doesn't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // Reuse the previously unzipped HTML if we already have it. The unzipped HTML is keyed only on the
  // book (it lives in the per-book cache dir), not on render settings, so it survives the invalidation
  // that wipes the layout (.bin) caches when font/margin/orientation change -- rebuilds then skip zip
  // inflation entirely. It's promoted by an atomic rename as soon as the inflate succeeds (below), so
  // even a window-only giant spine -- whose .bin never finalizes -- still caches its HTML, letting a
  // reopen skip the multi-second inflate. If htmlPath exists it is known-complete.
  const bool reusedHtml = Storage.exists(htmlPath.c_str());
  bool htmlCached = reusedHtml;
  if (reusedHtml) {
    LOG_DBG("SCT", "Reusing cached HTML %s", htmlPath.c_str());
  } else {
    Storage.mkdir(htmlDir.c_str());

    // Retry logic for SD card timing issues
    bool streamed = false;
    uint32_t fileSize = 0;
    for (int attempt = 0; attempt < 3 && !streamed; attempt++) {
      if (attempt > 0) {
        LOG_DBG("SCT", "Retrying stream (attempt %d)...", attempt + 1);
        delay(50);  // Brief delay before retry
      }

      // Remove any incomplete file from previous attempt before retrying
      if (Storage.exists(tmpHtmlPath.c_str())) {
        Storage.remove(tmpHtmlPath.c_str());
      }

      HalFile tmpHtml;
      if (!Storage.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
        continue;
      }
      // Larger chunks mean far fewer SD writes inflating the HTML; a 1KB chunk turned a 584KB
      // single-spine novel into ~570 tiny writes (multi-second). 8KB keeps the transient buffers
      // small while cutting the write count 8x.
      streamed = epub->readItemContentsToStream(localPath, tmpHtml, 8192);
      fileSize = tmpHtml.size();
      streamed = tmpHtml.sync() && streamed;
      // Explicitly close() file before calling Storage.remove()
      streamed = tmpHtml.close() && streamed;

      // If streaming failed, remove the incomplete file immediately
      if (!streamed && Storage.exists(tmpHtmlPath.c_str())) {
        Storage.remove(tmpHtmlPath.c_str());
        LOG_DBG("SCT", "Removed incomplete temp file after failed attempt");
      }
    }

    if (!streamed) {
      LOG_ERR("SCT", "Failed to stream item contents to temp file after retries free=%u largest=%u",
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      return false;
    }

    LOG_DBG("SCT", "Streamed temp HTML to %s (%d bytes)", tmpHtmlPath.c_str(), fileSize);

    // Promote to the persistent HTML cache immediately -- the inflate is complete and the bytes are
    // valid regardless of whether the layout build finishes, so reopening (even a window-only spine
    // that never finalizes its .bin) skips re-inflation. If the rename fails we just parse the temp.
    if (Storage.rename(tmpHtmlPath.c_str(), htmlPath.c_str())) {
      htmlCached = true;
    } else {
      LOG_DBG("SCT", "Failed to promote HTML cache; parsing from temp");
    }
  }

#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("SCT", "EPUB_BUILD stage=html mode=%s cached=%u free=%u largest=%u", reusedHtml ? "reused" : "new",
          htmlCached ? 1u : 0u, static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif

  if (!Storage.openFileForWrite("SCT", binTmpPath(), file)) {
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    LOG_ERR("SCT", "Failed to open section temp free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    return false;
  }
  // Header is written with the incomplete-version sentinel; finalizeBuild() commits it.
  if (!writeSectionFileHeader(spec)) {
    file.close();
    Storage.remove(binTmpPath().c_str());
    return false;
  }

  auto ctx = makeUniqueNoThrow<BuildContext>();
  if (!ctx) {
    LOG_ERR("SCT", "OOM: BuildContext free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  if (!Storage.openFileForWrite("SCT", lutTmpPath(), ctx->lut)) {
    file.close();
    Storage.remove(binTmpPath().c_str());
    Storage.remove(lutTmpPath().c_str());
    if (!htmlCached) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  // htmlCached == "htmlPath is the live cache" (reused, or just promoted). finalizeBuild/abandonBuild
  // then leave the cached HTML alone; only an un-promoted temp (rename failed) is theirs to clean up.
  ctx->reusedHtml = htmlCached;
  ctx->htmlPath = htmlPath;
  ctx->tmpHtmlPath = tmpHtmlPath;
  ctx->parsePath = htmlCached ? htmlPath : tmpHtmlPath;

  // Derive the content base directory and image cache path prefix for the parser
  const size_t lastSlash = localPath.find_last_of('/');
  ctx->contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  ctx->imageBasePath = epub->getCachePath() + (preview_ ? "/preview_img_" : "/img_") + std::to_string(spineIndex) + "_";

  ctx->spec = spec;
  if (!loadBuildCss(ctx.get())) {
    ctx->lut.close();
    Storage.remove(lutTmpPath().c_str());
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!ctx->reusedHtml) Storage.remove(ctx->tmpHtmlPath.c_str());
    return false;
  }

  ctx->parser = makeBuildParser(ctx.get(), spec, popupFn);
  if (!ctx->parser) {
    LOG_ERR("SCT", "OOM: ChapterHtmlSlimParser free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    if (ctx->cssParser) ctx->cssParser->clear();
    ctx->lut.close();
    Storage.remove(lutTmpPath().c_str());
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }

  Hyphenator::setPreferredLanguage(epub->getLanguage());
  build_ = std::move(ctx);

  if (!build_->parser->beginParse()) {
    LOG_ERR("SCT", "Failed to begin parse free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    abandonBuild();
    return false;
  }
  if (partial_) {
#ifdef TENOR_PRESS_PROBE
    const uint32_t restoreStarted = millis();
    const bool restored = restorePartialBuild();
    LOG_INF("SCT", "RESTORE ok=%u pages=%u ms=%lu", restored ? 1u : 0u, static_cast<unsigned>(partialPageCount_),
            static_cast<unsigned long>(millis() - restoreStarted));
    if (restored) {
#else
    if (restorePartialBuild()) {
#endif
      LOG_INF("SCT", "Resumed checkpoint: %u pages, HTML byte %u", builtPageCount_,
              static_cast<unsigned>(build_->parser->parseBytesConsumed()));
    } else {
      // A legacy or damaged checkpoint keeps the readable partial and starts a
      // fresh parser. Recreate both staging files after any failed prefix copy.
      build_->parser.reset();
      file.close();
      build_->lut.close();
      builtPageCount_ = 0;
      build_->failed = false;
      build_->lastVisibleTextOffset = 0;
      if (!Storage.openFileForWrite("SCT", binTmpPath(), file) || !writeSectionFileHeader(spec) ||
          !Storage.openFileForWrite("SCT", lutTmpPath(), build_->lut)) {
        abandonBuild();
        return false;
      }
      build_->parser = makeBuildParser(build_.get(), spec, popupFn);
      if (!build_->parser || !build_->parser->beginParse()) {
        abandonBuild();
        return false;
      }
#ifdef TENOR_PRESS_PROBE
      LOG_INF("SCT", "Partial checkpoint unavailable; rebuilding from chapter start pages=%u",
              static_cast<unsigned>(partialPageCount_));
#else
      LOG_DBG("SCT", "Partial checkpoint unavailable; rebuilding from chapter start");
#endif
    }
  }
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("SCT", "EPUB_BUILD stage=parser_ready free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  build_->totalBytes = build_->parser->parseTotalBytes();
  build_->bytesConsumed = build_->parser->parseBytesConsumed();
  return true;
}

bool Section::restorePartialBuild() {
  HalFile previous;
  if (!Storage.openFileForRead("SCT", filePath, previous)) return false;
  serialization::CheckedReader reader(previous);
  // Compare the complete render key, excluding version/count/LUT addresses.
  constexpr size_t KEY_BYTES = HEADER_SIZE - 1 - sizeof(uint16_t) - 5 * sizeof(uint32_t);
  uint8_t previousKey[KEY_BYTES], nextKey[KEY_BYTES];
  if (!reader.seek(1) || !reader.read(previousKey, KEY_BYTES) || !file.seek(1) ||
      file.read(nextKey, KEY_BYTES) != KEY_BYTES || memcmp(previousKey, nextKey, KEY_BYTES) != 0 ||
      !file.seek(HEADER_SIZE)) return false;
  uint16_t pages = 0;
  uint32_t pageLut = 0, anchors = 0, paragraphs = 0, items = 0, visible = 0;
  if (!reader.pod(pages) || pages != partialPageCount_ || !reader.pod(pageLut) || !reader.pod(anchors) ||
      !reader.pod(paragraphs) || !reader.pod(items) || !reader.pod(visible)) return false;
  const uint64_t count = pages;
  const uint64_t extension = visible + count * sizeof(uint32_t) + 2 * sizeof(uint32_t);
  if (pages == 0 || pageLut < HEADER_SIZE || anchors != pageLut + count * sizeof(uint32_t) ||
      paragraphs < anchors + sizeof(uint16_t) || items != paragraphs + sizeof(uint16_t) + count * sizeof(uint16_t) ||
      visible != items + count * sizeof(uint16_t) || extension >= previous.size() ||
      !reader.seek(extension) || !build_->parser->restoreCheckpoint(previous, pages)) return false;

  // Batch SD transfers while preserving page offsets and the atomic commit.
  // Release the optional buffer before allocating LUTs or parsing more text.
  uint8_t buffer[512];
  if (!reader.seek(HEADER_SIZE)) return false;
  {
    constexpr size_t COPY_BUFFER_BYTES = 4096;
    auto bulkBuffer = makeUniqueNoThrow<uint8_t[]>(COPY_BUFFER_BYTES);
    uint8_t* const copyBuffer = bulkBuffer ? bulkBuffer.get() : buffer;
    const size_t capacity = bulkBuffer ? COPY_BUFFER_BYTES : sizeof(buffer);
#ifdef TENOR_UI_ACCEPTANCE
    const uint32_t copyStart = millis();
#endif
    for (size_t remaining = pageLut - HEADER_SIZE; remaining > 0;) {
      const size_t bytes = std::min(remaining, capacity);
      if (!reader.read(copyBuffer, bytes) || file.write(copyBuffer, bytes) != bytes) return false;
      remaining -= bytes;
    }
#ifdef TENOR_UI_ACCEPTANCE
    LOG_DBG("SCT", "EPUB_PREFIX bytes=%u block=%u ms=%u free=%u largest=%u",
            static_cast<unsigned>(pageLut - HEADER_SIZE), static_cast<unsigned>(capacity),
            static_cast<unsigned>(millis() - copyStart), static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  }
  constexpr size_t ENTRIES_PER_BLOCK = 64;
  auto entries = makeUniqueNoThrow<PageLutEntry[]>(ENTRIES_PER_BLOCK);
  if (!entries) return false;
  uint32_t previousOffset = 0, previousVisible = 0;
  for (size_t first = 0; first < pages; first += ENTRIES_PER_BLOCK) {
    const size_t rows = std::min(ENTRIES_PER_BLOCK, static_cast<size_t>(pages) - first);
    const auto readColumn = [&](uint32_t start, const auto field) {
      using Value = std::remove_reference_t<decltype(entries[0].*field)>;
      const size_t bytes = rows * sizeof(Value);
      if (!reader.seek(start + first * sizeof(Value)) || !reader.read(buffer, bytes)) return false;
      for (size_t i = 0; i < rows; ++i) memcpy(&(entries[i].*field), buffer + i * sizeof(Value), sizeof(Value));
      return true;
    };
    if (!readColumn(pageLut, &PageLutEntry::fileOffset) ||
        !readColumn(paragraphs + sizeof(uint16_t), &PageLutEntry::paragraphIndex) ||
        !readColumn(items, &PageLutEntry::listItemIndex) ||
        !readColumn(visible, &PageLutEntry::visibleTextOffset)) return false;
    for (size_t i = 0; i < rows; ++i) {
      const auto& entry = entries[i];
      if (entry.fileOffset < HEADER_SIZE || entry.fileOffset >= pageLut || entry.fileOffset <= previousOffset ||
          entry.visibleTextOffset < previousVisible) return false;
      previousOffset = entry.fileOffset;
      previousVisible = entry.visibleTextOffset;
    }
    const size_t bytes = rows * sizeof(PageLutEntry);
    if (build_->lut.write(entries.get(), bytes) != bytes) return false;
  }
  builtPageCount_ = pages;
  build_->lastVisibleTextOffset = previousVisible;
  return previous.close();
}

bool Section::stepHeapAvailable() {
  return ESP.getFreeHeap() >= BUILD_STEP_MIN_FREE_HEAP && ESP.getMaxAllocHeap() >= BUILD_STEP_MIN_MAX_ALLOC;
}

// A checkpoint exists only right after the step that reached it, and a build yields on its time
// budget far more often than on a finished page. Parking there failed, and the partial written
// instead had no checkpoint, so every later turn laid the chapter out again from its first page
// (783 to 857 ms a turn on the X3). One more paragraph of parsing is far cheaper.
bool Section::reachCheckpoint() {
  auto& parser = *build_->parser;
  if (parser.hasCheckpoint()) return true;
  if (builtPageCount_ == 0 || !parser.requestCheckpoint()) return false;
  const uint32_t started = millis();
  unsigned steps = 0;
  while (!parser.hasCheckpoint() && steps < CHECKPOINT_REACH_MAX_STEPS &&
         millis() - started < CHECKPOINT_REACH_MAX_MS && stepHeapAvailable()) {
    const auto status = parser.parseStep();
    ++steps;
    if (status == ChapterHtmlSlimParser::ParseStatus::Error || build_->failed) break;
    if (status == ChapterHtmlSlimParser::ParseStatus::Done) {
      // The chapter ended first: the whole of it is laid out, nothing is left to hold.
      return finalizeBuild();
    }
  }
  build_->bytesConsumed = parser.parseBytesConsumed();
#ifdef TENOR_PRESS_PROBE
  LOG_INF("SCT", "CHECKPOINT_REACH ok=%u steps=%u ms=%lu pages=%u", parser.hasCheckpoint() ? 1u : 0u, steps,
          static_cast<unsigned long>(millis() - started), static_cast<unsigned>(builtPageCount_));
#endif
  return parser.hasCheckpoint();
}

bool Section::parkBuild() {
  if (!build_ || build_->failed) return false;
  if (!build_->parser) return true;
  if (!reachCheckpoint()) {
#ifdef TENOR_PRESS_PROBE
    LOG_INF("SCT", "PARK_FAIL reason=no_checkpoint pages=%u", static_cast<unsigned>(builtPageCount_));
#endif
    return false;
  }
  if (!build_) return true;
#ifdef TENOR_UI_ACCEPTANCE
  const auto started = millis();
#endif
  HalFile checkpoint;
  if (!Storage.openFileForWrite("SCT", checkpointTmpPath(), checkpoint)) return false;
  const bool written = build_->parser->writeCheckpoint(checkpoint) && checkpoint.sync();
#ifdef TENOR_UI_ACCEPTANCE
  const size_t bytes = checkpoint.size();
#endif
  const bool closed = checkpoint.close();
  if (!written || !closed) {
    Storage.remove(checkpointTmpPath().c_str());
#ifdef TENOR_PRESS_PROBE
    LOG_INF("SCT", "PARK_FAIL reason=write pages=%u", static_cast<unsigned>(builtPageCount_));
#endif
    return false;
  }
  build_->bytesConsumed = build_->parser->parseBytesConsumed();
  build_->parkedAnchors = build_->parser->takeAnchors();
  build_->parser.reset();
  if (build_->cssParser) build_->cssParser->clear();
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("SCT", "EPUB_PARK pages=%u checkpoint_bytes=%u ms=%u free=%u largest=%u",
          static_cast<unsigned>(builtPageCount_), static_cast<unsigned>(bytes),
          static_cast<unsigned>(millis() - started), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  return true;
}

bool Section::resumeParkedBuild() {
  if (!build_ || build_->failed) return false;
  if (build_->parser) return true;
#ifdef TENOR_UI_ACCEPTANCE
  const auto started = millis();
#endif
  HalFile checkpoint;
  if (!Storage.openFileForRead("SCT", checkpointTmpPath(), checkpoint)) return false;
  // The checkpoint restores anchors too. Release the parked map before allocating
  // the parser so the two copies never overlap in the render heap.
  std::vector<std::pair<std::string, uint16_t>>().swap(build_->parkedAnchors);
  bool restored = loadBuildCss(build_.get());
  if (restored) {
    build_->parser = makeBuildParser(build_.get(), build_->spec);
    restored = build_->parser && build_->parser->beginParse() &&
               build_->parser->restoreCheckpoint(checkpoint, builtPageCount_);
  }
  const bool closed = checkpoint.close();
  if (!restored || !closed) {
    build_->parser.reset();
    if (build_->cssParser) build_->cssParser->clear();
    return false;
  }
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("SCT", "EPUB_RESUME pages=%u ms=%u free=%u largest=%u", static_cast<unsigned>(builtPageCount_),
          static_cast<unsigned>(millis() - started), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  return true;
}

bool Section::buildSomeMore(const int maxPages) {
  if (!build_ || !resumeParkedBuild()) {
    LOG_ERR("SCT", "Unable to resume section build");
    if (build_) abandonBuild();
    return false;
  }
  // Pace on pages laid out by THIS build, not pageCount: during a rebuild over a partial,
  // pageCount stays pinned at the partial's watermark until the build passes it, which
  // would otherwise turn one "small" chunk into a blocking rebuild of the whole watermark.
  const int startCount = builtPageCount_;
  const uint32_t tickStart = millis();
  buildStarved_ = false;
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t inputBytes = build_->parser->parseBytesConsumed();
  LOG_DBG("SCT", "EPUB_TICK begin max=%d built=%u available=%u bytes=%u/%u free=%u largest=%u",
          maxPages, static_cast<unsigned>(builtPageCount_), static_cast<unsigned>(pageCount),
          static_cast<unsigned>(inputBytes), static_cast<unsigned>(build_->totalBytes),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  unsigned steps = 0;
  for (;;) {
    if (!stepHeapAvailable()) {
      LOG_ERR("SCT", "Build starved of heap free=%u largest=%u; parking after %u pages",
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
              static_cast<unsigned>(builtPageCount_));
      build_->bytesConsumed = build_->parser->parseBytesConsumed();
      buildStarved_ = true;
      // A failed park keeps the parser resident; either way the caller can retry.
      parkBuild();
      return false;
    }
    const auto status = build_->parser->parseStep();
    if (status == ChapterHtmlSlimParser::ParseStatus::Error || build_->failed) {
      LOG_ERR("SCT", "Parse error during incremental build free=%u largest=%u",
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      abandonBuild();
      return false;
    }
    if (status == ChapterHtmlSlimParser::ParseStatus::Done) {
#ifdef TENOR_UI_ACCEPTANCE
      LOG_DBG("SCT", "EPUB_TICK done max=%d built=%u delta=%u bytes=%u/%u steps=%u ms=%u free=%u largest=%u",
              maxPages, static_cast<unsigned>(builtPageCount_), static_cast<unsigned>(builtPageCount_ - startCount),
              static_cast<unsigned>(build_->parser->parseBytesConsumed()), static_cast<unsigned>(build_->totalBytes),
              steps, static_cast<unsigned>(millis() - tickStart), static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
      return finalizeBuild();
    }
    // ParseStatus::More: yield once we've laid out the requested number of pages.
    if (maxPages > 0 && ((builtPageCount_ - startCount) >= maxPages || ++steps >= 4 ||
                         millis() - tickStart >= 20)) {
      build_->bytesConsumed = build_->parser->parseBytesConsumed();
#ifdef TENOR_UI_ACCEPTANCE
      LOG_DBG("SCT", "EPUB_TICK yield max=%d built=%u delta=%u bytes=%u/%u steps=%u ms=%u last_visible=%u free=%u largest=%u",
              maxPages, static_cast<unsigned>(builtPageCount_), static_cast<unsigned>(builtPageCount_ - startCount),
              static_cast<unsigned>(build_->bytesConsumed), static_cast<unsigned>(build_->totalBytes), steps,
              static_cast<unsigned>(millis() - tickStart), static_cast<unsigned>(build_->lastVisibleTextOffset),
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
      return true;
    }
  }
}

bool Section::hasHtmlCache() const {
  const std::string htmlPath = epub->getCachePath() + "/html/" + std::to_string(spineIndex) + ".html";
  return Storage.exists(htmlPath.c_str());
}

std::optional<uint16_t> Section::findAnchorDuringBuild(const std::string& anchor) const {
  if (!build_) return std::nullopt;
  const auto& anchors = build_->parser ? build_->parser->getAnchors() : build_->parkedAnchors;
  for (const auto& [key, page] : anchors) {
    if (key == anchor) return page;
  }
  return std::nullopt;
}

std::optional<uint16_t> Section::findAnchor(const std::string& anchor) const {
  if (const auto page = findAnchorDuringBuild(anchor)) {
    return page;
  }
  // Fall back to the on-disk anchor map: a finalized section, or a partial whose map
  // covers everything up to its watermark (nullopt past it -- build further and retry).
  return getPageForAnchor(anchor);
}

float Section::laidOutFraction() const {
  if (build_ && builtPageCount_ >= pageCount && build_->totalBytes > 0) {
    return std::min(1.0f, static_cast<float>(build_->bytesConsumed) / static_cast<float>(build_->totalBytes));
  }
  if (partial_ && partialTotalBytes_ > 0) {
    return std::min(1.0f, static_cast<float>(partialBytesConsumed_) / static_cast<float>(partialTotalBytes_));
  }
  return build_ ? 0.0f : 1.0f;
}

uint16_t Section::pageAtFraction(const float fraction) const {
  if (pageCount == 0) return 0;
  const float laidOut = laidOutFraction();
  const float share = laidOut > 0 ? std::min(1.0f, fraction / laidOut) : 1.0f;
  const int page = static_cast<int>(share * static_cast<float>(pageCount));
  return static_cast<uint16_t>(std::min(page, static_cast<int>(pageCount) - 1));
}

bool Section::coversVisibleTextOffset(const uint32_t offset) const {
  if (pageCount < 2) return false;
  const auto lastStart = getVisibleTextOffsetForPage(pageCount - 1);
  return lastStart.has_value() && offset < *lastStart;
}

uint16_t Section::estimatedTotalPages() const {
  // Extrapolation from a suspended session's watermark trailer. A static snapshot, so no EMA
  // damping is needed. Also the best guess while a rebuild is running but hasn't laid out
  // enough pages yet to extrapolate from its own progress.
  const auto partialEstimate = [this]() -> uint16_t {
    if (!partial_ || partialBytesConsumed_ == 0 || partialTotalBytes_ <= partialBytesConsumed_) {
      return pageCount;
    }
    const uint64_t est = static_cast<uint64_t>(partialPageCount_) * partialTotalBytes_ / partialBytesConsumed_;
    if (est <= pageCount) return pageCount;
    return est > 60000 ? 60000 : static_cast<uint16_t>(est);
  };

  if (!build_) {
    return partial_ ? partialEstimate() : pageCount;  // partial -> extrapolate, finalized -> exact
  }
  const uint32_t consumed = build_->bytesConsumed;
  const uint32_t total = build_->totalBytes;
  if (builtPageCount_ == 0 || consumed == 0 || total <= consumed) return partialEstimate();

  // Raw extrapolation: scale the pages built so far by the fraction of HTML still unparsed. This
  // re-derives from a growing, non-uniform sample, so it jitters up and down as the build crosses
  // dense vs sparse regions of the chapter.
  const uint64_t raw = static_cast<uint64_t>(builtPageCount_) * total / consumed;

  // Damp that jitter with an exponential moving average. Step it once per build advance (keyed on
  // bytesConsumed) rather than per status-bar redraw, so the smoothing rate doesn't depend on how
  // often we repaint. As the build nears the end, consumed -> total and raw -> the built count, so
  // the average settles onto the true count (and finalizeBuild then returns the exact pageCount).
  constexpr float ALPHA = 0.25f;  // weight of each new sample; lower = steadier but slower to settle
  if (build_->smoothedEstimate <= 0) {
    build_->smoothedEstimate = static_cast<float>(raw);  // seed on the first estimate
  } else if (consumed != build_->smoothedAtConsumed) {
    build_->smoothedEstimate += ALPHA * (static_cast<float>(raw) - build_->smoothedEstimate);
  }
  build_->smoothedAtConsumed = consumed;

  const uint64_t est = static_cast<uint64_t>(build_->smoothedEstimate + 0.5f);
  if (est <= pageCount) return pageCount;  // never fewer than the pages already available
  return est > 60000 ? 60000 : static_cast<uint16_t>(est);
}

// Write the LUTs and anchor map into the open tmp .bin, patch the header with the built
// page count and table offsets, stamp `version` as the commit point, then swap the tmp
// file over filePath. For SECTION_FILE_PARTIAL_VERSION a watermark trailer
// (bytesConsumed, totalBytes) is appended after the li LUT so a later open can estimate
// the total page count. The parser must still be alive (anchors are read from it).
// On failure the tmp is removed and any pre-existing file at filePath is left intact.
bool Section::commitBuildFile(const uint8_t version, const uint32_t bytesConsumed, const uint32_t totalBytes) {
  const bool asPartial = (version == SECTION_FILE_PARTIAL_VERSION);
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t commitStart = millis();
  LOG_DBG("SCT", "EPUB_CACHE_WRITE begin partial=%u pages=%u bytes=%u/%u free=%u largest=%u",
          asPartial ? 1u : 0u, static_cast<unsigned>(builtPageCount_), static_cast<unsigned>(bytesConsumed),
          static_cast<unsigned>(totalBytes), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  const auto failCommit = [this]() {
    build_->failed = true;
    file.close();
    Storage.remove(binTmpPath().c_str());
#ifdef TENOR_UI_ACCEPTANCE
    LOG_DBG("SCT", "EPUB_CACHE_WRITE failed pages=%u free=%u largest=%u", static_cast<unsigned>(builtPageCount_),
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
    return false;
  };
  if (build_->failed || build_->parser->hasFailed() || !file || !build_->lut) return failCommit();

  // Fixed 768-byte heap workspace, independent of chapter length and render-task stack.
  constexpr size_t ENTRIES_PER_BLOCK = 64;
  auto entries = makeUniqueNoThrow<PageLutEntry[]>(ENTRIES_PER_BLOCK);
  if (!entries) return failCommit();
  const auto writeColumn = [&](const auto field) {
    if (!build_->lut.seek(0)) return false;
    for (size_t offset = 0; offset < builtPageCount_; offset += ENTRIES_PER_BLOCK) {
      const size_t count = std::min(ENTRIES_PER_BLOCK, static_cast<size_t>(builtPageCount_) - offset);
      const size_t bytes = count * sizeof(PageLutEntry);
      if (build_->lut.read(entries.get(), bytes) != static_cast<int>(bytes)) return false;
      // Compact one column in the existing workspace, behind the unread entries.
      // Each output value is at most four bytes, while each input row is twelve.
      auto* output = reinterpret_cast<uint8_t*>(entries.get());
      const size_t valueBytes = sizeof(entries[0].*field);
      for (size_t i = 0; i < count; ++i) {
        if (entries[i].fileOffset == 0) return false;
        const auto value = entries[i].*field;
        memcpy(output + i * valueBytes, &value, valueBytes);
      }
      const size_t outputBytes = count * valueBytes;
      if (file.write(output, outputBytes) != outputBytes) return false;
    }
    return true;
  };
  const uint32_t lutOffset = file.position();
  if (!writeColumn(&PageLutEntry::fileOffset)) return failCommit();

  const uint32_t anchorMapOffset = file.position();
  const auto& anchors = build_->parser->getAnchors();
  uint32_t anchorCount = 0;
  for (const auto& [anchor, page] : anchors) {
    if (!asPartial || page < builtPageCount_) ++anchorCount;
  }
  if (anchorCount > UINT16_MAX || !serialization::writePod(file, static_cast<uint16_t>(anchorCount)))
    return failCommit();
  for (const auto& [anchor, page] : anchors) {
    if (asPartial && page >= builtPageCount_) continue;
    if (!serialization::writeString(file, anchor) || !serialization::writePod(file, page)) return failCommit();
  }

  const uint32_t paragraphLutOffset = file.position();
  if (!serialization::writePod(file, builtPageCount_) || !writeColumn(&PageLutEntry::paragraphIndex)) return failCommit();
  const uint32_t liLutFileOffset = file.position();
  if (!writeColumn(&PageLutEntry::listItemIndex)) return failCommit();
  const uint32_t visibleLutFileOffset = file.position();
  if (!writeColumn(&PageLutEntry::visibleTextOffset)) return failCommit();
  if (asPartial && (!serialization::writePod(file, bytesConsumed) || !serialization::writePod(file, totalBytes)))
    return failCommit();
  if (asPartial && build_->parser->hasCheckpoint() && !build_->parser->writeCheckpoint(file)) return failCommit();

  if (!file.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(builtPageCount_)) ||
      !serialization::writePod(file, builtPageCount_) || !serialization::writePod(file, lutOffset) ||
      !serialization::writePod(file, anchorMapOffset) || !serialization::writePod(file, paragraphLutOffset) ||
      !serialization::writePod(file, liLutFileOffset) || !serialization::writePod(file, visibleLutFileOffset) ||
      !file.seek(0) || !serialization::writePod(file, version) || !file.sync()) return failCommit();
  if (!file.close()) return failCommit();

  // Keep the previously committed cache recoverable through replacement failure.
  bool backupCleanupPending = false;
  if (!freeink::replaceFile(Storage, binTmpPath().c_str(), filePath.c_str(), &backupCleanupPending)) return failCommit();
  if (backupCleanupPending) LOG_DBG("SCT", "Section committed with retained recovery backup");
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("SCT", "EPUB_CACHE_WRITE done partial=%u pages=%u ms=%u free=%u largest=%u",
          asPartial ? 1u : 0u, static_cast<unsigned>(builtPageCount_), static_cast<unsigned>(millis() - commitStart),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  return true;
}

bool Section::finalizeBuild() {
  // Flush the trailing page (emits the last page via the completePageFn into the LUT).
  if (!build_->parser->finishParse() || build_->failed) {
    abandonBuild();
    return false;
  }

  if (!build_->reusedHtml) {
    // Parse succeeded: promote the freshly unzipped HTML to the persistent cache so future
    // rebuilds skip zip inflation. If promotion fails, drop the temp -- the build still succeeded.
    if (!Storage.rename(build_->tmpHtmlPath.c_str(), build_->htmlPath.c_str())) {
      LOG_DBG("SCT", "Failed to promote HTML cache, removing temp");
      Storage.remove(build_->tmpHtmlPath.c_str());
    }
  }

  const bool committed = commitBuildFile(SECTION_FILE_VERSION, 0, 0);
  if (build_->cssParser) build_->cssParser->clear();
  build_->lut.close();
  Storage.remove(lutTmpPath().c_str());
  Storage.remove(checkpointTmpPath().c_str());
  build_.reset();
  if (!committed) {
    pageCount = partial_ ? partialPageCount_ : 0;
    builtPageCount_ = 0;
    return false;
  }
  buildComplete_ = true;
  partial_ = false;
  partialPageCount_ = 0;
  pageCount = builtPageCount_;
  return true;
}

void Section::suspendBuild() {
  if (!build_) return;
#ifdef TENOR_PRESS_PROBE
  // Teardown cost on the exit path: a parked build is reloaded only to be written out.
  const uint32_t suspendStarted = millis();
  const bool wasParked = !build_->parser;
#endif
  if (build_->failed || !resumeParkedBuild() || build_->parser->hasFailed()) {
    abandonBuild();
    return;
  }
  // The partial written below carries a checkpoint only when the parser sits at one; without it
  // the next open lays the chapter out again from its first page.
  // The chapter may end on the way: finalized (or failed to finalize), nothing is left to suspend.
  if (builtPageCount_ > 0) reachCheckpoint();
  if (!build_) return;

  // Only worth persisting if this build produced pages a pre-existing partial doesn't
  // already cover; otherwise keep the older (bigger) partial and just drop the tmp.
  const bool worthKeeping = builtPageCount_ > 0 && (!partial_ || builtPageCount_ > partialPageCount_);

  bool committed = false;
  if (worthKeeping) {
    // Capture the parse watermark and commit before tearing the parser down (the anchor
    // map is read from it). The incomplete trailing page is intentionally not flushed:
    // only fully laid-out pages are persisted, and the rebuild re-derives the rest.
    const uint32_t consumed = static_cast<uint32_t>(build_->parser->parseBytesConsumed());
    committed = commitBuildFile(SECTION_FILE_PARTIAL_VERSION, consumed, build_->totalBytes);
    if (committed) {
      partial_ = true;
      partialPageCount_ = builtPageCount_;
      partialBytesConsumed_ = consumed;
      partialTotalBytes_ = build_->totalBytes;
      LOG_INF("SCT", "Suspended build: %u pages persisted", builtPageCount_);
    }
  }

  if (build_->parser) build_->parser->abortParse();
  if (build_->cssParser) build_->cssParser->clear();
  if (!committed && file) {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
    Storage.remove(binTmpPath().c_str());
  }
  if (!build_->reusedHtml && Storage.exists(build_->tmpHtmlPath.c_str())) {
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  build_->lut.close();
  Storage.remove(lutTmpPath().c_str());
  Storage.remove(checkpointTmpPath().c_str());
#ifdef TENOR_PRESS_PROBE
  LOG_INF("SCT", "SUSPEND ms=%lu parked=%u committed=%u pages=%u", static_cast<unsigned long>(millis() - suspendStarted),
          wasParked ? 1u : 0u, committed ? 1u : 0u, static_cast<unsigned>(builtPageCount_));
#endif
  build_.reset();
  buildComplete_ = false;
  pageCount = partial_ ? partialPageCount_ : 0;
  builtPageCount_ = 0;
}

void Section::abandonBuild() {
  if (!build_) return;
  if (build_->parser) build_->parser->abortParse();
  if (build_->cssParser) build_->cssParser->clear();
  if (file) {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
    Storage.remove(binTmpPath().c_str());
  }
  if (!build_->reusedHtml && Storage.exists(build_->tmpHtmlPath.c_str())) {
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  build_->lut.close();
  Storage.remove(lutTmpPath().c_str());
  Storage.remove(checkpointTmpPath().c_str());
  build_.reset();
  buildComplete_ = false;
  pageCount = partial_ ? partialPageCount_ : 0;
  builtPageCount_ = 0;
}

bool Section::readBuildEntry(const uint16_t page, PageLutEntry& entry) const {
  return build_ && page < builtPageCount_ &&
         build_->lut.seek(static_cast<size_t>(page) * sizeof(entry)) &&
         serialization::readPod(build_->lut, entry);
}

std::unique_ptr<Page> Section::loadPageDuringBuild(const int page) {
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t readStart = millis();
#endif
  PageLutEntry entry{};
  if (page < 0 || !file || !readBuildEntry(page, entry) || entry.fileOffset == 0) return nullptr;
  const uint32_t writePos = file.position();
  if (!file.seek(entry.fileOffset)) return nullptr;
  auto p = Page::deserialize(file);
  if (!file.seek(writePos)) {
    build_->failed = true;
    if (build_->parser) build_->parser->failBuild();
    return nullptr;
  }
  if (p) p->visibleTextOffset = entry.visibleTextOffset;
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("SCT", "EPUB_CACHE_READ source=active page=%d ok=%u file_offset=%u visible=%u ms=%u free=%u largest=%u",
          page, p ? 1u : 0u, static_cast<unsigned>(entry.fileOffset), static_cast<unsigned>(entry.visibleTextOffset),
          static_cast<unsigned>(millis() - readStart), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  return p;
}

// Read a page from the committed file at filePath (finalized section or partial from a
// previous session). Uses a local handle so it is safe while a build holds the member
// `file` open on the tmp .bin.
std::unique_ptr<Page> Section::loadPageAt(const int page) const {
#ifdef TENOR_UI_ACCEPTANCE
  const uint32_t readStart = millis();
#endif
  HalFile f;
  if (page < 0 || !Storage.openFileForRead("SCT", filePath, f)) return nullptr;
  serialization::CheckedReader reader(f);
#ifdef TENOR_UI_ACCEPTANCE
  unsigned seekCount = 0;
#endif
  const auto seek = [&reader
#ifdef TENOR_UI_ACCEPTANCE
                     , &seekCount
#endif
  ](const size_t target) {
#ifdef TENOR_UI_ACCEPTANCE
    ++seekCount;
#endif
    return reader.seek(target);
  };
  uint16_t count = 0;
  uint32_t lutOffset = 0, pagePos = 0, visibleLutOffset = 0, visibleTextOffset = 0;
  if (!seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t)) || !reader.pod(count) ||
      page >= count || !reader.pod(lutOffset) || lutOffset < HEADER_SIZE ||
      static_cast<uint64_t>(lutOffset) + sizeof(uint32_t) * count > f.size() ||
      !seek(lutOffset + sizeof(uint32_t) * page) || !reader.pod(pagePos) ||
      pagePos < HEADER_SIZE || pagePos >= lutOffset ||
      !seek(HEADER_SIZE - sizeof(uint32_t)) || !reader.pod(visibleLutOffset) ||
      visibleLutOffset < lutOffset ||
      static_cast<uint64_t>(visibleLutOffset) + sizeof(uint32_t) * count > f.size() ||
      !seek(visibleLutOffset + sizeof(uint32_t) * page) || !reader.pod(visibleTextOffset) ||
      !seek(pagePos)) return nullptr;
  auto result = Page::deserialize(f);
  if (result) result->visibleTextOffset = visibleTextOffset;
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("SCT", "EPUB_CACHE_READ source=disk page=%d ok=%u count=%u seeks=%u file_offset=%u visible=%u ms=%u free=%u largest=%u",
          page, result ? 1u : 0u, static_cast<unsigned>(count), static_cast<unsigned>(seekCount),
          static_cast<unsigned>(pagePos), static_cast<unsigned>(visibleTextOffset),
          static_cast<unsigned>(millis() - readStart),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
  return result;
}

std::unique_ptr<Page> Section::loadPage(const int page) {
  if (page < 0) {
    return nullptr;
  }
  if (build_ && page < static_cast<int>(builtPageCount_)) {
    return loadPageDuringBuild(page);
  }
  // Not (yet) in the active build: serve from the file on disk -- a finalized section,
  // or a partial from a previous session whose pages the rebuild hasn't reached again.
  const int onDisk = partial_ ? partialPageCount_ : (build_ ? 0 : pageCount);
  if (page >= onDisk) {
    return nullptr;
  }
  return loadPageAt(page);
}

std::string Section::getTextFromSectionFile() {
  std::string fullText;
  auto p = loadPage(currentPage);
  if (p) {
    for (const auto& el : p->elements) {
      if (el->getTag() == TAG_PageLine) {
        const auto& line = static_cast<const PageLine&>(*el);
        if (line.getBlock()) {
          const auto& block = *line.getBlock();
          for (uint16_t i = 0; i < block.wordCount(); i++) {
            if (!fullText.empty()) fullText += " ";
            fullText += block.wordText(i);
          }
        }
      }
    }
  }
  return fullText;
}

std::optional<uint16_t> Section::getCachedPageCount() const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (fileSize < HEADER_SIZE) {
    return std::nullopt;
  }

  // Only a finalized section's count is the chapter total; a partial's count is just the
  // suspended build's watermark, which would skew progress mapping. Callers fall back to
  // their own estimates.
  uint8_t version;
  if (!serialization::readPod(f, version)) return std::nullopt;
  if (version != SECTION_FILE_VERSION) {
    return std::nullopt;
  }

  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t))) return std::nullopt;
  uint16_t count;
  if (!serialization::readPod(f, count)) return std::nullopt;
  return count;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 4)) return std::nullopt;
  uint32_t anchorMapOffset;
  if (!serialization::readPod(f, anchorMapOffset)) return std::nullopt;
  if (anchorMapOffset == 0 || anchorMapOffset >= fileSize) {
    return std::nullopt;
  }

  if (!f.seek(anchorMapOffset)) return std::nullopt;
  uint16_t count;
  if (!serialization::readPod(f, count)) return std::nullopt;
  for (uint16_t i = 0; i < count; i++) {
    std::string key;
    uint16_t page;
    if (!serialization::readString(f, key)) return std::nullopt;
    if (!serialization::readPod(f, page)) return std::nullopt;
    if (key == anchor) {
      return page;
    }
  }

  return std::nullopt;
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 3)) return std::nullopt;
  uint32_t paragraphLutOffset;
  if (!serialization::readPod(f, paragraphLutOffset)) return std::nullopt;
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  if (!f.seek(paragraphLutOffset)) return std::nullopt;
  uint16_t count;
  if (!serialization::readPod(f, count)) return std::nullopt;
  if (count == 0) {
    return std::nullopt;
  }

  const uint64_t lutEnd = static_cast<uint64_t>(paragraphLutOffset) + sizeof(uint16_t) + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pagePIdx;
    if (!serialization::readPod(f, pagePIdx)) return std::nullopt;
    if (pagePIdx >= pIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}

std::optional<uint16_t> Section::getParagraphIndexForPage(const uint16_t page) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 3)) return std::nullopt;
  uint32_t paragraphLutOffset;
  if (!serialization::readPod(f, paragraphLutOffset)) return std::nullopt;
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  if (!f.seek(paragraphLutOffset)) return std::nullopt;
  uint16_t count;
  if (!serialization::readPod(f, count)) return std::nullopt;
  if (count == 0 || page >= count) {
    return std::nullopt;
  }

  const uint64_t entryEnd = static_cast<uint64_t>(paragraphLutOffset) + sizeof(uint16_t) + (page + 1) * sizeof(uint16_t);
  if (entryEnd > fileSize) {
    return std::nullopt;
  }

  if (!f.seek(static_cast<uint64_t>(paragraphLutOffset) + sizeof(uint16_t) + page * sizeof(uint16_t))) return std::nullopt;
  uint16_t pIdx;
  if (!serialization::readPod(f, pIdx)) return std::nullopt;
  return pIdx;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 2)) return std::nullopt;
  uint32_t liLutOffset;
  if (!serialization::readPod(f, liLutOffset)) return std::nullopt;
  if (liLutOffset == 0 || liLutOffset >= fileSize) {
    return std::nullopt;
  }

  // The li LUT shares count with the paragraph LUT; read count from paragraphLutOffset
  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 3)) return std::nullopt;
  uint32_t paragraphLutOffset;
  if (!serialization::readPod(f, paragraphLutOffset)) return std::nullopt;
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  if (!f.seek(paragraphLutOffset)) return std::nullopt;
  uint16_t count;
  if (!serialization::readPod(f, count)) return std::nullopt;
  if (count == 0) {
    return std::nullopt;
  }

  const uint64_t lutEnd = static_cast<uint64_t>(liLutOffset) + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  if (!f.seek(liLutOffset)) return std::nullopt;
  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pageLiIdx;
    if (!serialization::readPod(f, pageLiIdx)) return std::nullopt;
    if (pageLiIdx >= liIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}

std::optional<uint32_t> Section::getVisibleTextOffsetForPage(const uint16_t page) const {
  if (build_ && page < builtPageCount_) {
    PageLutEntry entry{};
    if (!readBuildEntry(page, entry)) return std::nullopt;
    return entry.visibleTextOffset;
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f) || f.size() < HEADER_SIZE) {
    return std::nullopt;
  }

  uint8_t version;
  if (!serialization::readPod(f, version)) return std::nullopt;
  if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
    return std::nullopt;
  }

  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t))) return std::nullopt;
  uint16_t count;
  if (!serialization::readPod(f, count)) return std::nullopt;
  if (page >= count) {
    return std::nullopt;
  }

  if (!f.seek(HEADER_SIZE - sizeof(uint32_t))) return std::nullopt;
  uint32_t visibleLutOffset;
  if (!serialization::readPod(f, visibleLutOffset)) return std::nullopt;
  const uint64_t entryOffset = static_cast<uint64_t>(visibleLutOffset) + static_cast<uint32_t>(page) * sizeof(uint32_t);
  if (visibleLutOffset < HEADER_SIZE || entryOffset + sizeof(uint32_t) > f.size()) {
    return std::nullopt;
  }

  if (!f.seek(entryOffset)) return std::nullopt;
  uint32_t result;
  if (!serialization::readPod(f, result)) return std::nullopt;
  return result;
}

std::optional<uint16_t> Section::getPageForVisibleTextOffset(const uint32_t offset,
                                                             const bool preferFirstAtOffset) const {
  if (build_ && builtPageCount_ > 0 && offset <= build_->lastVisibleTextOffset) {
    // Lower/upper bound over the temporary LUT. Equal offsets can represent image-only pages.
    uint32_t low = 0, high = builtPageCount_;
    while (low < high) {
      const uint32_t mid = low + (high - low) / 2;
      PageLutEntry entry{};
      if (!readBuildEntry(static_cast<uint16_t>(mid), entry)) return std::nullopt;
      if (entry.visibleTextOffset < offset || (!preferFirstAtOffset && entry.visibleTextOffset == offset))
        low = mid + 1;
      else
        high = mid;
    }
    if (preferFirstAtOffset && low < builtPageCount_) {
      PageLutEntry entry{};
      if (!readBuildEntry(static_cast<uint16_t>(low), entry)) return std::nullopt;
      if (entry.visibleTextOffset == offset) return static_cast<uint16_t>(low);
    }
    return static_cast<uint16_t>(low == 0 ? 0 : low - 1);
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f) || f.size() < HEADER_SIZE) {
    return std::nullopt;
  }

  uint8_t version;
  if (!serialization::readPod(f, version)) return std::nullopt;
  if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
    return std::nullopt;
  }
  const bool partial = version == SECTION_FILE_PARTIAL_VERSION;

  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t))) return std::nullopt;
  uint16_t count;
  if (!serialization::readPod(f, count)) return std::nullopt;
  if (count == 0) {
    return std::nullopt;
  }

  if (!f.seek(HEADER_SIZE - sizeof(uint32_t))) return std::nullopt;
  uint32_t visibleLutOffset;
  if (!serialization::readPod(f, visibleLutOffset)) return std::nullopt;
  if (visibleLutOffset < HEADER_SIZE || static_cast<uint64_t>(visibleLutOffset) + static_cast<uint32_t>(count) * sizeof(uint32_t) > f.size()) {
    return std::nullopt;
  }

  if (!f.seek(visibleLutOffset)) return std::nullopt;
  uint16_t result = 0;
  uint32_t lastPageStart = 0;
  for (uint16_t page = 0; page < count; page++) {
    uint32_t pageStart;
    if (!serialization::readPod(f, pageStart)) return std::nullopt;
    lastPageStart = pageStart;
    if (preferFirstAtOffset && pageStart == offset) {
      return page;
    }
    if (pageStart > offset) break;
    result = page;
  }
  if (partial && offset > lastPageStart) {
    return std::nullopt;
  }
  return result;
}
