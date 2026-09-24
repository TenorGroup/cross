#include "BookMetadataCache.h"

#include <Arduino.h>
#include <BufferedFile.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>
#include <ZipFile.h>

#include <deque>

#include "FsHelpers.h"

namespace {
constexpr uint8_t BOOK_CACHE_VERSION = 10;  // v10: ignore ambiguous guide text references
constexpr char bookBinFile[] = "/book.bin";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
// Buffer size for the buildBookBin streams. 3 buffers x 4KB, transient (freed on
// return); 4KB = 8 SD sectors per transfer, enough to stop the sector-cache thrash.
constexpr size_t BUILD_IO_BUFFER_SIZE = 4096;
// Heap kept free while the per-spine indexes grow. The firmware has no
// exceptions, so a std::deque that cannot get memory aborts the device; the
// passes below size their working sets from free heap minus this reserve and
// refuse the book instead of aborting when even a small working set won't fit.
constexpr uint32_t INDEX_HEAP_RESERVE = 16 * 1024;
constexpr uint32_t BOOKBIN_HEAP_RESERVE = 32 * 1024;
// book.bin is written in chunks of spine items; each item in a chunk holds a
// zip lookup target, its inflated size and its first TOC index.
constexpr size_t BOOKBIN_ITEM_BYTES = sizeof(ZipFile::SizeTarget) + sizeof(uint32_t) + sizeof(int16_t);
constexpr int BOOKBIN_MIN_CHUNK = 64;

#ifdef TENOR_UI_ACCEPTANCE
// Logical metadata operations. BufferedFileReader/Writer may coalesce these
// requests before they reach the underlying HalFile.
struct CacheIoStats {
  uint32_t readOps = 0;
  uint32_t writeOps = 0;
  uint32_t seekOps = 0;
  uint64_t readBytes = 0;
  uint64_t writeBytes = 0;
};

CacheIoStats cacheIoStats;

struct CacheIoSnapshot {
  CacheIoStats stats;
};

CacheIoSnapshot snapshotCacheIo() { return {cacheIoStats}; }

void logCacheIoDelta(const char* op, const int index, const bool ok, const CacheIoSnapshot& before) {
  LOG_DBG("BMC", "EPUB_BMC_IO op=%s index=%d ok=%u read_ops=%u read_bytes=%llu write_ops=%u write_bytes=%llu seek_ops=%u",
          op, index, ok ? 1u : 0u, cacheIoStats.readOps - before.stats.readOps,
          static_cast<unsigned long long>(cacheIoStats.readBytes - before.stats.readBytes),
          cacheIoStats.writeOps - before.stats.writeOps,
          static_cast<unsigned long long>(cacheIoStats.writeBytes - before.stats.writeBytes),
          cacheIoStats.seekOps - before.stats.seekOps);
}
#endif

// Cache strings are metadata, never chapter content. Bound allocation before resize,
// and keep failures sticky so a failed seek cannot reinterpret the previous record.
constexpr uint32_t MAX_METADATA_STRING_BYTES = 4096;
template <typename F>
class MetadataReader {
 public:
  MetadataReader(F& file, size_t end) : file(file), end(end) {}
  bool ok() const { return valid; }
  size_t position() const { return file.position(); }
  bool seek(size_t target) {
#ifdef TENOR_UI_ACCEPTANCE
    ++cacheIoStats.seekOps;
#endif
    valid = valid && target <= end && file.seek(target);
    return valid;
  }
  template <typename T>
  bool pod(T& value) {
    value = {};
    return read(&value, sizeof(value));
  }
  bool string(std::string& value) {
    uint32_t length = 0;
    if (!pod(length) || length > MAX_METADATA_STRING_BYTES || !has(length)) {
      valid = false;
      value.clear();
      return false;
    }
    value.resize(length);
    return read(value.data(), length);
  }

  bool read(void* dst, size_t length) {
#ifdef TENOR_UI_ACCEPTANCE
    ++cacheIoStats.readOps;
    cacheIoStats.readBytes += length;
#endif
    valid = valid && has(length) && file.read(dst, length) == length;
    return valid;
  }

 private:
  bool has(size_t length) const { return position() <= end && length <= end - position(); }
  F& file;
  size_t end;
  bool valid = true;
};

// Entry (de)serializers, templated so they run over HalFile and the Buffered*
// wrappers alike (two instantiations each -- a few hundred bytes of flash, in
// exchange for the build path streaming at SD speed instead of per-pod).
template <typename F>
uint32_t writeSpineEntryTo(F& file, const BookMetadataCache::SpineEntry& entry) {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.href);
  serialization::writePod(file, entry.cumulativeSize);
  serialization::writePod(file, entry.tocIndex);
#ifdef TENOR_UI_ACCEPTANCE
  ++cacheIoStats.writeOps;
  cacheIoStats.writeBytes += file.position() - pos;
#endif
  return pos;
}

template <typename F>
uint32_t writeTocEntryTo(F& file, const BookMetadataCache::TocEntry& entry) {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.title);
  serialization::writeString(file, entry.href);
  serialization::writeString(file, entry.anchor);
  serialization::writePod(file, entry.level);
  serialization::writePod(file, entry.spineIndex);
#ifdef TENOR_UI_ACCEPTANCE
  ++cacheIoStats.writeOps;
  cacheIoStats.writeBytes += file.position() - pos;
#endif
  return pos;
}

template <typename F>
BookMetadataCache::SpineEntry readSpineEntryFrom(F& file) {
  BookMetadataCache::SpineEntry entry;
  file.string(entry.href);
  file.pod(entry.cumulativeSize);
  file.pod(entry.tocIndex);
  return file.ok() ? entry : decltype(entry){};
}

template <typename F>
BookMetadataCache::TocEntry readTocEntryFrom(F& file) {
  BookMetadataCache::TocEntry entry;
  file.string(entry.title);
  file.string(entry.href);
  file.string(entry.anchor);
  file.pod(entry.level);
  file.pod(entry.spineIndex);
  return file.ok() ? entry : decltype(entry){};
}
}  // namespace

/* ============= WRITING / BUILDING FUNCTIONS ================ */

bool BookMetadataCache::beginWrite() {
#ifdef TENOR_UI_ACCEPTANCE
  cacheIoStats = {};
#endif
  buildMode = true;
  spineCount = 0;
  tocCount = 0;
  LOG_DBG("BMC", "Entering write mode");
  return true;
}

bool BookMetadataCache::beginContentOpfPass() {
  LOG_DBG("BMC", "Beginning content opf pass");

  // Open spine file for writing
  if (!Storage.openFileForWrite("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  // Wrapper OOM is fine: createSpineEntry falls back to unbuffered writes.
  passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(spineFile, BUILD_IO_BUFFER_SIZE);
  return true;
}

bool BookMetadataCache::endContentOpfPass() {
  const bool flushed = !passOut || passOut->flush();
  passOut.reset();
  // Explicit close() required: member variable persists beyond function scope
  spineFile.close();
  if (!flushed) {
    LOG_ERR("BMC", "Failed writing spine tmp file");
  }
  return flushed;
}

bool BookMetadataCache::beginTocPass() {
  LOG_DBG("BMC", "Beginning toc pass");

  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  if (!Storage.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) {
    // Explicit close() required: member variable persists beyond function scope
    spineFile.close();
    return false;
  }

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    // Without the index every TOC entry rescans the spine file (minutes at
    // thousands of chapters), so a book whose index does not fit is refused.
    const uint32_t needed = static_cast<uint32_t>(spineCount) * sizeof(SpineHrefIndexEntry) + INDEX_HEAP_RESERVE;
    if (ESP.getFreeHeap() < needed) {
      LOG_ERR("BMC", "Book too large: %d spine items need %u B of heap, %u free", spineCount,
              static_cast<unsigned>(needed), static_cast<unsigned>(ESP.getFreeHeap()));
      tocFile.close();
      spineFile.close();
      return false;
    }
    spineHrefIndex.clear();
    spineHrefIndex.resize(spineCount);
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      spineHrefIndex[i] = hrefKey(readSpineEntry(spineFile).href, static_cast<int16_t>(i));
    }
    std::sort(spineHrefIndex.begin(), spineHrefIndex.end());
    spineFile.seek(0);
    useSpineHrefIndex = true;
    LOG_DBG("BMC", "Using fast index for %d spine items", spineCount);
  } else {
    useSpineHrefIndex = false;
  }

  // Wrapper OOM is fine: createTocEntry falls back to unbuffered writes.
  passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(tocFile, BUILD_IO_BUFFER_SIZE);
  return true;
}

bool BookMetadataCache::endTocPass() {
  const bool flushed = !passOut || passOut->flush();
  passOut.reset();
  if (!flushed) {
    LOG_ERR("BMC", "Failed writing toc tmp file");
  }
  // Explicit close() required: member variables persist beyond function scope
  tocFile.close();
  spineFile.close();

  spineHrefIndex.clear();
  spineHrefIndex.shrink_to_fit();
  useSpineHrefIndex = false;

  return flushed;
}

bool BookMetadataCache::endWrite() {
  if (!buildMode) {
    LOG_DBG("BMC", "endWrite called but not in build mode");
    return false;
  }

  buildMode = false;
  LOG_DBG("BMC", "Wrote %d spine, %d TOC entries", spineCount, tocCount);
#ifdef TENOR_UI_ACCEPTANCE
  LOG_DBG("BMC", "EPUB_BMC_IO op=build_total index=-1 ok=1 read_ops=%u read_bytes=%llu write_ops=%u write_bytes=%llu seek_ops=%u",
          cacheIoStats.readOps, static_cast<unsigned long long>(cacheIoStats.readBytes), cacheIoStats.writeOps,
          static_cast<unsigned long long>(cacheIoStats.writeBytes), cacheIoStats.seekOps);
#endif
  return true;
}

bool BookMetadataCache::buildBookBin(const std::string& epubPath, const BookMetadata& metadata) {
  // Open all three files, writing to meta, reading from spine and toc
  if (!Storage.openFileForWrite("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    // Explicit close() required: member variable persists beyond function scope
    bookFile.close();
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + tmpTocBinFile, tocFile)) {
    // Explicit close() required: member variables persist beyond function scope
    bookFile.close();
    spineFile.close();
    return false;
  }

  // Buffered streams for the whole build: every access below is sequential per
  // file, but interleaved ACROSS files, which thrashes SdFat's single shared
  // sector cache when unbuffered (one 512B SD transaction per 4-byte pod --
  // measured 31s for a 1,732-spine omnibus). Three 4KB buffers, freed on return.
  serialization::BufferedFileWriter bookOut(bookFile, BUILD_IO_BUFFER_SIZE);
  serialization::BufferedFileReader spineBuffer(spineFile, BUILD_IO_BUFFER_SIZE);
  MetadataReader spineIn(spineBuffer, spineFile.size());
  serialization::BufferedFileReader tocBuffer(tocFile, BUILD_IO_BUFFER_SIZE);
  MetadataReader tocIn(tocBuffer, tocFile.size());

  constexpr uint32_t headerASize =
      sizeof(BOOK_CACHE_VERSION) + /* LUT Offset */ sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount);
  const uint32_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                                metadata.coverItemHref.size() + metadata.textReferenceHref.size() +
                                sizeof(uint32_t) * 5;
  const uint32_t lutSize = sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount;
  const uint32_t lutOffset = headerASize + metadataSize;

  // Header A
  serialization::writePod(bookOut, BOOK_CACHE_VERSION);
  serialization::writePod(bookOut, lutOffset);
  serialization::writePod(bookOut, spineCount);
  serialization::writePod(bookOut, tocCount);
  // Metadata
  serialization::writeString(bookOut, metadata.title);
  serialization::writeString(bookOut, metadata.author);
  serialization::writeString(bookOut, metadata.language);
  serialization::writeString(bookOut, metadata.coverItemHref);
  serialization::writeString(bookOut, metadata.textReferenceHref);

  // Loop through spine entries, writing LUT positions
  spineIn.seek(0);
  for (int i = 0; i < spineCount; i++) {
    const uint32_t pos = spineIn.position();
    readSpineEntryFrom(spineIn);
    serialization::writePod(bookOut, pos + lutOffset + lutSize);
  }
  // Total size of the spine tmp file: entries land in book.bin after the toc LUT
  // and the full spine block, so toc LUT positions are offset by it.
  const auto spineBytes = static_cast<uint32_t>(spineIn.position());

  // Loop through toc entries, writing LUT positions
  tocIn.seek(0);
  for (int i = 0; i < tocCount; i++) {
    const uint32_t pos = tocIn.position();
    readTocEntryFrom(tocIn);
    serialization::writePod(bookOut, pos + lutOffset + lutSize + spineBytes);
  }

  // LUTs complete. Spine entries are written in chunks sized from free heap so
  // the working set stays bounded at any chapter count. A book that fits one
  // chunk takes exactly the passes it always took: one TOC scan for the
  // spine->TOC mapping and one zip central-directory scan for the sizes.
  const bool useBatchSizes = spineCount >= LARGE_SPINE_THRESHOLD;
  int chunk = spineCount;
  if (useBatchSizes) {
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t budget = freeHeap > BOOKBIN_HEAP_RESERVE ? freeHeap - BOOKBIN_HEAP_RESERVE : 0;
    chunk = static_cast<int>(std::min<uint32_t>(spineCount, budget / BOOKBIN_ITEM_BYTES));
  }
#ifdef TENOR_PRESS_PROBE
  LOG_INF("BMC", "BOOKBIN spines=%d toc=%d chunk=%d free=%u largest=%u min=%u", spineCount, tocCount, chunk,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          static_cast<unsigned>(ESP.getMinFreeHeap()));
#endif

  ZipFile zip(epubPath);
  // Pre-open zip file to speed up size calculations
  if (chunk < std::min<int>(spineCount, BOOKBIN_MIN_CHUNK) || !zip.open()) {
    LOG_ERR("BMC", "Could not size spine items: chunk %d, heap %u", chunk, static_cast<unsigned>(ESP.getFreeHeap()));
    // Explicit close() required: member variables persist beyond function scope
    bookFile.close();
    spineFile.close();
    tocFile.close();
    Storage.remove((cachePath + bookBinFile).c_str());
    return false;
  }
  // NOTE: We intentionally skip calling loadAllFileStatSlims() here.
  // For large EPUBs (2000+ chapters), pre-loading all ZIP central directory entries
  // into memory causes OOM crashes on ESP32-C3's limited ~380KB RAM.
  // Instead, for large books we use a batch lookup that scans the ZIP
  // central directory once per chunk and matches against spine targets using hash comparison.
  // This is O(n*log(m)) instead of O(n*m) while avoiding memory exhaustion.
  // See: https://github.com/crosspoint-reader/crosspoint-reader/issues/134

  uint32_t cumSize = 0;
  int lastSpineTocIndex = -1;
  spineIn.seek(0);
  for (int first = 0; first < spineCount; first += chunk) {
    const int count = std::min(chunk, spineCount - first);

    // First TOC entry of each spine item in this chunk, in one TOC pass.
    std::deque<int16_t> spineToTocIndex(count, -1);
    tocIn.seek(0);
    for (int j = 0; j < tocCount; j++) {
      const int local = readTocEntryFrom(tocIn).spineIndex - first;
      if (local >= 0 && local < count && spineToTocIndex[local] == -1) {
        spineToTocIndex[local] = static_cast<int16_t>(j);
      }
    }

    std::deque<uint32_t> spineSizes(count, 0);
    if (useBatchSizes) {
      const size_t chunkStart = spineIn.position();
      std::deque<ZipFile::SizeTarget> targets(count);
      for (int i = 0; i < count; i++) {
        const std::string path = FsHelpers::normalisePath(readSpineEntryFrom(spineIn).href);
        targets[i] = {ZipFile::fnvHash64(path.c_str(), path.size()), static_cast<uint16_t>(path.size()),
                      static_cast<uint16_t>(i)};
      }
      std::sort(targets.begin(), targets.end(), [](const ZipFile::SizeTarget& a, const ZipFile::SizeTarget& b) {
        return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
      });
      const int matched = zip.fillUncompressedSizes(targets, spineSizes);
      LOG_DBG("BMC", "Batch lookup matched %d/%d spine items", matched, count);
      (void)matched;
      spineIn.seek(chunkStart);
    }

    for (int i = 0; i < count; i++) {
      auto spineEntry = readSpineEntryFrom(spineIn);
      spineEntry.tocIndex = spineToTocIndex[i];

      // Not a huge deal if we don't fine a TOC entry for the spine entry, this is expected behaviour for EPUBs
      // Logging here is for debugging
      if (spineEntry.tocIndex == -1) {
        LOG_DBG("BMC", "Warning: Could not find TOC entry for spine item %d: %s, using title from last section",
                first + i, spineEntry.href.c_str());
        spineEntry.tocIndex = lastSpineTocIndex;
      }
      lastSpineTocIndex = spineEntry.tocIndex;

      // Small books look every item up directly; a batch miss falls back the same way.
      size_t itemSize = spineSizes[i];
      if (itemSize == 0) {
        const std::string path = FsHelpers::normalisePath(spineEntry.href);
        if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
          LOG_ERR("BMC", "Warning: Could not get size for spine item: %s", path.c_str());
        }
      }

      cumSize += itemSize;
      spineEntry.cumulativeSize = cumSize;

      // Write out spine data to book.bin
      writeSpineEntryTo(bookOut, spineEntry);
    }
  }
  // Close opened zip file
  zip.close();

  // Loop through toc entries from toc file writing to book.bin
  tocIn.seek(0);
  for (int i = 0; i < tocCount; i++) {
    auto tocEntry = readTocEntryFrom(tocIn);
    writeTocEntryTo(bookOut, tocEntry);
  }

  const bool written = bookOut.flush() && spineIn.ok() && tocIn.ok();

  // Explicit close() required: member variables persist beyond function scope
  bookFile.close();
  spineFile.close();
  tocFile.close();

  if (!written) {
    // A short write (card full/removed) would leave a truncated book.bin that
    // still passes the version check on load; remove it so the next open rebuilds.
    LOG_ERR("BMC", "Failed writing book.bin, removing truncated file");
    Storage.remove((cachePath + bookBinFile).c_str());
    return false;
  }

  LOG_DBG("BMC", "Successfully built book.bin");
  return true;
}

bool BookMetadataCache::cleanupTmpFiles() const {
  const auto spineBinFile = cachePath + tmpSpineBinFile;
  if (Storage.exists(spineBinFile.c_str())) {
    Storage.remove(spineBinFile.c_str());
  }
  const auto tocBinFile = cachePath + tmpTocBinFile;
  if (Storage.exists(tocBinFile.c_str())) {
    Storage.remove(tocBinFile.c_str());
  }
  return true;
}

uint32_t BookMetadataCache::writeSpineEntry(HalFile& file, const SpineEntry& entry) const {
  return writeSpineEntryTo(file, entry);
}

uint32_t BookMetadataCache::writeTocEntry(HalFile& file, const TocEntry& entry) const {
  return writeTocEntryTo(file, entry);
}

// Note: for the LUT to be accurate, this **MUST** be called for all spine items before `addTocEntry` is ever called
// this is because in this function we're marking positions of the items
void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (!buildMode || !spineFile) {
    LOG_DBG("BMC", "createSpineEntry called but not in build mode");
    return;
  }

  const SpineEntry entry(href, 0, -1);
  if (passOut) {
    writeSpineEntryTo(*passOut, entry);
  } else {
    writeSpineEntry(spineFile, entry);
  }
  spineCount++;
}

void BookMetadataCache::createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                       const uint8_t level) {
  if (!buildMode || !tocFile || !spineFile) {
    LOG_DBG("BMC", "createTocEntry called but not in build mode");
    return;
  }

  int16_t spineIndex = -1;

  if (useSpineHrefIndex) {
    const auto key = hrefKey(href, 0);
    const auto it = std::lower_bound(spineHrefIndex.begin(), spineHrefIndex.end(), key);
    if (it != spineHrefIndex.end() && it->sameKey(key)) {
      spineIndex = it->spineIndex;
    }

    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  } else {
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      auto spineEntry = readSpineEntry(spineFile);
      if (spineEntry.href == href) {
        spineIndex = static_cast<int16_t>(i);
        break;
      }
    }
    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  }

  // Compose the title to NFC at index time so the cache stores precomposed glyphs;
  // device fonts have no combining-mark positioning, so NFD titles render broken.
  const TocEntry entry(utf8ComposeNfc(title), href, anchor, level, spineIndex);
  if (passOut) {
    writeTocEntryTo(*passOut, entry);
  } else {
    writeTocEntry(tocFile, entry);
  }
  tocCount++;
}

/* ============= READING / LOADING FUNCTIONS ================ */

bool BookMetadataCache::load() {
  loaded = false;
  tocCursor.reset();
  cumulativeSizes.reset();
  itemSizes.reset();
  spineCount = tocCount = 0;
  if (bookFile) bookFile.close();
  if (!Storage.openFileForRead("BMC", cachePath + bookBinFile, bookFile)) return false;

  const auto fail = [this]() {
    LOG_ERR("BMC", "Invalid or unreadable book.bin; rejecting cache");
    tocCursor.reset();
    bookFile.close();
    cumulativeSizes.reset();
    itemSizes.reset();
    spineCount = tocCount = 0;
    return false;
  };
  const size_t fileSize = bookFile.size();
  MetadataReader header(bookFile, fileSize);
  uint8_t version = 0;
  if (!header.pod(version) || version != BOOK_CACHE_VERSION || !header.pod(lutOffset) || !header.pod(spineCount) ||
      !header.pod(tocCount))
    return fail();
  if (!header.string(coreMetadata.title) || !header.string(coreMetadata.author) ||
      !header.string(coreMetadata.language) || !header.string(coreMetadata.coverItemHref) ||
      !header.string(coreMetadata.textReferenceHref))
    return fail();

  // Indices on disk are signed 16-bit. Check arithmetic before any allocation.
  const uint32_t lutSize = (static_cast<uint32_t>(spineCount) + tocCount) * sizeof(uint32_t);
  if (spineCount == 0 || spineCount > INT16_MAX || tocCount > INT16_MAX || lutOffset != header.position() ||
      lutOffset > fileSize || lutSize > fileSize - lutOffset)
    return fail();

  itemSizes = makeUniqueNoThrow<uint16_t[]>(spineCount);
  cumulativeSizes =
      makeUniqueNoThrow<uint32_t[]>(itemSizes ? (spineCount + CUMULATIVE_STRIDE - 1) / CUMULATIVE_STRIDE : spineCount);
  if (!cumulativeSizes) return fail();

  // Two sequential streams validate every LUT pointer and record, including the
  // TOC tail. Fixed 4KB buffers avoid per-field SD reads; no whole-book allocation.
  HalFile lutFile;
  if (!Storage.openFileForRead("BMC", cachePath + bookBinFile, lutFile) || !lutFile.seek(lutOffset) ||
      !bookFile.seek(lutOffset + lutSize))
    return fail();
  serialization::BufferedFileReader lutBuffer(lutFile, BUILD_IO_BUFFER_SIZE);
  serialization::BufferedFileReader dataBuffer(bookFile, BUILD_IO_BUFFER_SIZE);
  MetadataReader lut(lutBuffer, lutOffset + lutSize);
  MetadataReader data(dataBuffer, fileSize);
  uint32_t previous = 0;
  for (uint16_t i = 0; i < spineCount; ++i) {
    uint32_t offset = 0;
    if (!lut.pod(offset) || offset != data.position()) return fail();
    const auto entry = readSpineEntryFrom(data);
    if (!data.ok() || entry.tocIndex < -1 || entry.tocIndex >= static_cast<int>(tocCount) ||
        entry.cumulativeSize < previous)
      return fail();
    if (itemSizes && entry.cumulativeSize - previous > UINT16_MAX) {
      // First chapter of 64 KB or more: expand what was read into a flat table.
      auto flat = makeUniqueNoThrow<uint32_t[]>(spineCount);
      if (!flat) return fail();
      for (uint16_t k = 0; k < i; ++k) {
        flat[k] = k % CUMULATIVE_STRIDE == 0 ? cumulativeSizes[k / CUMULATIVE_STRIDE] : flat[k - 1] + itemSizes[k];
      }
      cumulativeSizes = std::move(flat);
      itemSizes.reset();
    }
    if (!itemSizes) {
      cumulativeSizes[i] = entry.cumulativeSize;
    } else {
      itemSizes[i] = static_cast<uint16_t>(entry.cumulativeSize - previous);
      if (i % CUMULATIVE_STRIDE == 0) cumulativeSizes[i / CUMULATIVE_STRIDE] = entry.cumulativeSize;
    }
    previous = entry.cumulativeSize;
  }
  for (uint16_t i = 0; i < tocCount; ++i) {
    uint32_t offset = 0;
    if (!lut.pod(offset) || offset != data.position()) return fail();
    const auto entry = readTocEntryFrom(data);
    if (!data.ok() || entry.spineIndex < -1 || entry.spineIndex >= static_cast<int>(spineCount)) return fail();
  }
  if (data.position() != fileSize) return fail();
  loaded = true;
  LOG_DBG("BMC", "Validated cache: %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

BookMetadataCache::TocCursor::TocCursor(const std::string& path, uint32_t lut, int start, uint16_t spine, uint16_t toc)
    : lutOffset(lut), index(start), spineCount(spine), tocCount(toc) {
  if (start < 0 || start >= toc || !Storage.openFileForRead("BMC", path, file)) return;
  end = file.size();
  error = false;
  seekTo(start);
}

bool BookMetadataCache::TocCursor::seekTo(const int target) {
  if (error || target < 0 || target >= tocCount) return false;
  if (stream && target == index) return true;
  if (windowStart < 0 || target < windowStart || target >= windowStart + windowCount) {
    // Release the data wrapper before directly accessing its file for the LUT.
    stream.reset();
    windowStart = target / LOOKUP_WINDOW * LOOKUP_WINDOW;
    windowCount = std::min<int>(LOOKUP_WINDOW, tocCount - windowStart);
    MetadataReader reader(file, end);
    const uint32_t dataStart = lutOffset + (static_cast<uint32_t>(spineCount) + tocCount) * sizeof(uint32_t);
    if (!reader.seek(lutOffset + (static_cast<uint32_t>(spineCount) + windowStart) * sizeof(uint32_t)) ||
        !reader.read(offsets, windowCount * sizeof(uint32_t))) {
      error = true;
      return false;
    }
    for (uint16_t i = 0; i < windowCount; ++i) {
      if (offsets[i] < dataStart || offsets[i] >= end || (i > 0 && offsets[i] <= offsets[i - 1])) {
        error = true;
        return false;
      }
    }
    if (!reader.seek(offsets[0])) {
      error = true;
      return false;
    }
    stream = makeUniqueNoThrow<serialization::BufferedFileReader>(file, 2048);
    // Fill from the start of the window so backward lookups reuse earlier bytes.
    uint8_t firstByte = 0;
    if (!stream || stream->read(&firstByte, 1) != 1) {
      error = true;
      return false;
    }
  }
  if (!stream->seek(offsets[target - windowStart])) {
    error = true;
    return false;
  }
  index = target;
  return true;
}

bool BookMetadataCache::TocCursor::next(TocEntry& entry) {
  if (error || index >= tocCount) return false;
  MetadataReader reader(*stream, end);
  entry = readTocEntryFrom(reader);
  if (!reader.ok() || entry.spineIndex < -1 || entry.spineIndex >= spineCount ||
      (index + 1 == tocCount && reader.position() != end)) {
    error = true;
    entry = {};
    return false;
  }
  ++index;
  return true;
}

std::unique_ptr<BookMetadataCache::TocCursor> BookMetadataCache::openTocCursor(int start) const {
#ifdef TENOR_UI_ACCEPTANCE
  const auto ioBefore = snapshotCacheIo();
#endif
  if (!loaded || start < 0 || start >= tocCount) {
#ifdef TENOR_UI_ACCEPTANCE
    logCacheIoDelta("open_toc_cursor", start, false, ioBefore);
#endif
    return nullptr;
  }
  auto cursor = makeUniqueNoThrow<TocCursor>(cachePath + bookBinFile, lutOffset, start, spineCount, tocCount);
  if (!cursor || cursor->failed()) {
#ifdef TENOR_UI_ACCEPTANCE
    logCacheIoDelta("open_toc_cursor", start, false, ioBefore);
#endif
    return nullptr;
  }
#ifdef TENOR_UI_ACCEPTANCE
  logCacheIoDelta("open_toc_cursor", start, true, ioBefore);
#endif
  return cursor;
}

uint32_t BookMetadataCache::getCumulativeSize(const int index) const {
  if (!loaded || !cumulativeSizes || index < 0 || index >= spineCount) {
    return 0;
  }
  if (!itemSizes) return cumulativeSizes[index];
  const int first = index / CUMULATIVE_STRIDE * CUMULATIVE_STRIDE;
  uint32_t total = cumulativeSizes[first / CUMULATIVE_STRIDE];
  for (int i = first + 1; i <= index; ++i) total += itemSizes[i];
  return total;
}

BookMetadataCache::SpineEntry BookMetadataCache::getSpineEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getSpineEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(spineCount)) {
    LOG_ERR("BMC", "getSpineEntry index %d out of range", index);
    return {};
  }

#ifdef TENOR_UI_ACCEPTANCE
  const auto ioBefore = snapshotCacheIo();
#endif

  MetadataReader reader(bookFile, bookFile.size());
  uint32_t pos = 0;
  const uint32_t dataStart = lutOffset + (static_cast<uint32_t>(spineCount) + tocCount) * sizeof(uint32_t);
  if (!reader.seek(lutOffset + sizeof(uint32_t) * index) || !reader.pod(pos) || pos < dataStart || !reader.seek(pos)) {
#ifdef TENOR_UI_ACCEPTANCE
    logCacheIoDelta("get_spine_entry", index, false, ioBefore);
#endif
    return {};
  }
  auto entry = readSpineEntryFrom(reader);
  if (!reader.ok() || entry.tocIndex < -1 || entry.tocIndex >= static_cast<int>(tocCount)) {
#ifdef TENOR_UI_ACCEPTANCE
    logCacheIoDelta("get_spine_entry", index, false, ioBefore);
#endif
    return {};
  }
#ifdef TENOR_UI_ACCEPTANCE
  logCacheIoDelta("get_spine_entry", index, true, ioBefore);
#endif
  return entry;
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getTocEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(tocCount)) {
    LOG_ERR("BMC", "getTocEntry index %d out of range", index);
    return {};
  }

#ifdef TENOR_UI_ACCEPTANCE
  const auto ioBefore = snapshotCacheIo();
#endif

  if (!tocCursor) tocCursor = openTocCursor(index);
  if (tocCursor) {
    TocEntry entry;
    if (tocCursor->seekTo(index) && tocCursor->next(entry)) {
#ifdef TENOR_UI_ACCEPTANCE
      logCacheIoDelta("get_toc_entry_cursor", index, true, ioBefore);
#endif
      return entry;
    }
    tocCursor.reset();
  }

  MetadataReader reader(bookFile, bookFile.size());
  uint32_t pos = 0;
  const uint32_t dataStart = lutOffset + (static_cast<uint32_t>(spineCount) + tocCount) * sizeof(uint32_t);
  if (!reader.seek(lutOffset + sizeof(uint32_t) * (spineCount + index)) || !reader.pod(pos) || pos < dataStart ||
      !reader.seek(pos)) {
#ifdef TENOR_UI_ACCEPTANCE
    logCacheIoDelta("get_toc_entry_random", index, false, ioBefore);
#endif
    return {};
  }
  auto entry = readTocEntryFrom(reader);
  if (!reader.ok() || entry.spineIndex < -1 || entry.spineIndex >= static_cast<int>(spineCount)) {
#ifdef TENOR_UI_ACCEPTANCE
    logCacheIoDelta("get_toc_entry_random", index, false, ioBefore);
#endif
    return {};
  }
#ifdef TENOR_UI_ACCEPTANCE
  logCacheIoDelta("get_toc_entry_random", index, true, ioBefore);
#endif
  return entry;
}

BookMetadataCache::SpineEntry BookMetadataCache::readSpineEntry(HalFile& file) const {
  MetadataReader reader(file, file.size());
  return readSpineEntryFrom(reader);
}

BookMetadataCache::TocEntry BookMetadataCache::readTocEntry(HalFile& file) const {
  MetadataReader reader(file, file.size());
  return readTocEntryFrom(reader);
}
