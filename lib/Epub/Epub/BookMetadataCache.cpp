#include "BookMetadataCache.h"

#include <Arduino.h>
#include <BufferedFile.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>
#include <ZipFile.h>

#include <deque>

#include "FsHelpers.h"

#ifdef TENOR_PRESS_PROBE
IndexProbe indexProbe;

namespace {
// Where a pass's card writes went slow or short: a card busy past SdFat's write timeout fails the
// write, and the pass with it (X3 r08, "Failed writing toc tmp file").
void logPassWrites(const char* file, const serialization::BufferedFileWriter* out, const unsigned long flushMs) {
  LOG_INF("BMC", "INDEX_WRITE file=%s flush_ms=%lu slowest_write_ms=%lu short_at=%ld", file, flushMs,
          out ? out->slowestWriteMs : 0ul, out ? out->firstShortAt : -1l);
}
}  // namespace
#endif

namespace {
constexpr uint8_t BOOK_CACHE_VERSION = 10;  // v10: ignore ambiguous guide text references
constexpr char bookBinFile[] = "/book.bin";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
// A book indexed in the background (BookMetadataCache::indexesInBackground). book.part holds the
// chapters and the metadata in book.bin's layout under its own version byte, so neither ever
// passes for the other. The TOC pass lands in toc.bin, and toc.count, written after it, says it
// is whole. book.bin is built under a temporary name and renamed once whole.
constexpr uint8_t BOOK_PART_VERSION = 0x81;
constexpr char bookPartFile[] = "/book.part";
constexpr char tmpBookPartFile[] = "/book.part.tmp";
constexpr char tocDoneFile[] = "/toc.bin";
constexpr char tocCountFile[] = "/toc.count";
constexpr char tmpBookBinFile[] = "/book.bin.tmp";
// Where a background book.bin build stands between two steps: the chunks written to
// book.bin.tmp and the TOC matches so far. Rewritten (through a temporary name) after each chunk.
constexpr char bookCkptFile[] = "/book.ckpt";
constexpr char tmpBookCkptFile[] = "/book.ckpt.tmp";
constexpr uint8_t BOOK_CKPT_VERSION = 1;
constexpr uint8_t TOC_COUNT_VERSION = 1;
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

namespace {
struct BookBinCheckpoint {
  uint16_t spines = 0, tocs = 0, spinesDone = 0;
  int16_t lastSpineTocIndex = -1;
  uint32_t cumSize = 0, outBytes = 0, spineInPos = 0;
};

template <typename T>
bool writeField(HalFile& file, const T& value) {
  return file.write(&value, sizeof(value)) == sizeof(value);
}
template <typename T>
bool readField(HalFile& file, T& value) {
  return file.read(&value, sizeof(value)) == static_cast<int>(sizeof(value));
}

bool saveCheckpoint(const std::string& cachePath, const BookBinCheckpoint& c, const int16_t* tocSpine) {
  const std::string tmp = cachePath + tmpBookCkptFile;
  const std::string path = cachePath + bookCkptFile;
  HalFile file;
  if (!Storage.openFileForWrite("BMC", tmp, file)) return false;
  bool ok = writeField(file, BOOK_CKPT_VERSION) && writeField(file, c.spines) && writeField(file, c.tocs) &&
            writeField(file, c.spinesDone) && writeField(file, c.lastSpineTocIndex) && writeField(file, c.cumSize) &&
            writeField(file, c.outBytes) && writeField(file, c.spineInPos);
  const size_t tocBytes = sizeof(int16_t) * c.tocs;
  ok = ok && (tocBytes == 0 || file.write(tocSpine, tocBytes) == tocBytes);
  ok = file.close() && ok;
  Storage.remove(path.c_str());
  ok = ok && Storage.rename(tmp.c_str(), path.c_str());
  if (!ok) Storage.remove(tmp.c_str());
  return ok;
}

// A checkpoint of this very build: same chapter and TOC counts, whole.
bool loadCheckpoint(const std::string& cachePath, const uint16_t spines, const uint16_t tocs, BookBinCheckpoint& c,
                    int16_t* tocSpine) {
  HalFile file;
  if (!Storage.openFileForRead("BMC", cachePath + bookCkptFile, file)) return false;
  uint8_t version = 0;
  bool ok = readField(file, version) && version == BOOK_CKPT_VERSION && readField(file, c.spines) &&
            readField(file, c.tocs) && c.spines == spines && c.tocs == tocs && readField(file, c.spinesDone) &&
            readField(file, c.lastSpineTocIndex) && readField(file, c.cumSize) && readField(file, c.outBytes) &&
            readField(file, c.spineInPos) && c.spinesDone <= spines;
  const size_t tocBytes = sizeof(int16_t) * tocs;
  ok = ok && (tocBytes == 0 || file.read(tocSpine, tocBytes) == static_cast<int>(tocBytes)) &&
       file.position() == file.size();
  file.close();
  return ok;
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
#ifdef TENOR_PRESS_PROBE
  const unsigned long flushStarted = millis();
#endif
  const bool flushed = !passOut || passOut->flush();
#ifdef TENOR_PRESS_PROBE
  const unsigned long closeStarted = millis();
  logPassWrites("spine", passOut.get(), closeStarted - flushStarted);
#endif
  passOut.reset();
  // Explicit close() required: member variable persists beyond function scope
  spineFile.close();
#ifdef TENOR_PRESS_PROBE
  LOG_INF("BMC", "INDEX_WRITE file=spine close_ms=%lu", millis() - closeStarted);
#endif
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

#ifdef TENOR_PRESS_PROBE
  const unsigned long indexStarted = millis();
#endif
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
#ifdef TENOR_PRESS_PROBE
  LOG_INF("BMC", "INDEX_SPLIT href_index ms=%lu spines=%d", millis() - indexStarted, spineCount);
#endif

  // Wrapper OOM is fine: createTocEntry falls back to unbuffered writes.
  passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(tocFile, BUILD_IO_BUFFER_SIZE);
  return true;
}

bool BookMetadataCache::endTocPass() {
#ifdef TENOR_PRESS_PROBE
  const unsigned long flushStarted = millis();
#endif
  const bool flushed = !passOut || passOut->flush();
#ifdef TENOR_PRESS_PROBE
  logPassWrites("toc", passOut.get(), millis() - flushStarted);
#endif
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
  return buildBookBinFrom(epubPath, metadata,
                          {tmpSpineBinFile, 0, tmpTocBinFile, bookBinFile, false, spineCount, tocCount}, nullptr);
}

bool BookMetadataCache::buildBookBinFrom(const std::string& epubPath, const BookMetadata& metadata,
                                         const BookBinSource& source, const StopFn stop) {
#ifdef TENOR_PRESS_PROBE
  const unsigned long lutsStarted = millis();
  unsigned long tocScanMs = 0, zipSizeMs = 0, spineOutMs = 0;
  int chunks = 0;
#endif
  const uint16_t spines = source.spines;
  const uint16_t tocs = source.tocs;
  const std::string outPath = cachePath + source.outFile;
  // The chapter of every TOC entry, when the TOC pass left them unmatched: two bytes per entry,
  // taken before the chunk budget below.
  std::unique_ptr<int16_t[]> tocSpine;
  if (source.resolveToc && tocs > 0) {
    tocSpine = makeUniqueNoThrow<int16_t[]>(tocs);
    if (!tocSpine) {
      LOG_ERR("BMC", "No heap for the TOC match");
      return false;
    }
    std::fill(tocSpine.get(), tocSpine.get() + tocs, static_cast<int16_t>(-1));
  }
  // A background build (resolveToc) keeps a checkpoint at each chunk boundary, so a key press costs
  // the chunk under way instead of the whole build: 8 to 10 s steps on the X3 never finished for a
  // reader turning a page every 7 s.
  const bool resumable = source.resolveToc;
  BookBinCheckpoint ckpt;
  bool resuming = resumable && loadCheckpoint(cachePath, spines, tocs, ckpt, tocSpine.get());
  // Open all three files, writing to meta, reading from spine and toc
  HalFile outFile, spineSrc, tocSrc;
  if (resuming) {
    // What the checkpoint covers is on the card; what a cut step wrote after it is written again.
    outFile = Storage.open(outPath.c_str(), O_RDWR);
    resuming = outFile && outFile.size() >= ckpt.outBytes && outFile.seek(ckpt.outBytes);
    if (!resuming) {
      if (outFile) outFile.close();
      ckpt = BookBinCheckpoint{};
      if (tocSpine) std::fill(tocSpine.get(), tocSpine.get() + tocs, static_cast<int16_t>(-1));
    }
  }
  if (!resuming && !Storage.openFileForWrite("BMC", outPath, outFile)) {
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + source.spineFile, spineSrc)) {
    outFile.close();
    Storage.remove(outPath.c_str());
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + source.tocFile, tocSrc)) {
    outFile.close();
    spineSrc.close();
    Storage.remove(outPath.c_str());
    return false;
  }

  // Buffered streams for the whole build: every access below is sequential per
  // file, but interleaved ACROSS files, which thrashes SdFat's single shared
  // sector cache when unbuffered (one 512B SD transaction per 4-byte pod --
  // measured 31s for a 1,732-spine omnibus). Three 4KB buffers, freed on return.
  serialization::BufferedFileWriter bookOut(outFile, BUILD_IO_BUFFER_SIZE);
  serialization::BufferedFileReader spineBuffer(spineSrc, BUILD_IO_BUFFER_SIZE);
  MetadataReader spineIn(spineBuffer, spineSrc.size());
  serialization::BufferedFileReader tocBuffer(tocSrc, BUILD_IO_BUFFER_SIZE);
  MetadataReader tocIn(tocBuffer, tocSrc.size());

  // A failed build leaves nothing: the output is removed and its name never read.
  auto abandon = [&]() {
    outFile.close();
    spineSrc.close();
    tocSrc.close();
    Storage.remove(outPath.c_str());
    if (resumable) Storage.remove((cachePath + bookCkptFile).c_str());
    return false;
  };
  // A stopped one-pass build does the same; a stopped background build keeps its checkpoint.
  auto giveUp = [&]() {
    if (!resumable) return abandon();
    outFile.close();
    spineSrc.close();
    tocSrc.close();
    return false;
  };
  auto stopped = [&](const int i) { return stop && (i & 255) == 0 && stop(); };
  auto checkpoint = [&](const int done, const uint32_t cum, const int lastToc) {
    ckpt = {spines, tocs, static_cast<uint16_t>(done), static_cast<int16_t>(lastToc), cum,
            static_cast<uint32_t>(bookOut.position()), static_cast<uint32_t>(spineIn.position())};
    return bookOut.flush() && outFile.sync() && saveCheckpoint(cachePath, ckpt, tocSpine.get());
  };

  constexpr uint32_t headerASize =
      sizeof(BOOK_CACHE_VERSION) + /* LUT Offset */ sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount);
  const uint32_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                                metadata.coverItemHref.size() + metadata.textReferenceHref.size() +
                                sizeof(uint32_t) * 5;
  const uint32_t lutSize = sizeof(uint32_t) * spines + sizeof(uint32_t) * tocs;
  const uint32_t lutOffset = headerASize + metadataSize;

  if (!resuming) {
  // Header A
  serialization::writePod(bookOut, BOOK_CACHE_VERSION);
  serialization::writePod(bookOut, lutOffset);
  serialization::writePod(bookOut, spines);
  serialization::writePod(bookOut, tocs);
  // Metadata
  serialization::writeString(bookOut, metadata.title);
  serialization::writeString(bookOut, metadata.author);
  serialization::writeString(bookOut, metadata.language);
  serialization::writeString(bookOut, metadata.coverItemHref);
  serialization::writeString(bookOut, metadata.textReferenceHref);

  // Loop through spine entries, writing LUT positions
  spineIn.seek(source.spineStart);
  for (int i = 0; i < spines; i++) {
    if (stopped(i)) return giveUp();
    const uint32_t pos = spineIn.position() - source.spineStart;
    readSpineEntryFrom(spineIn);
    serialization::writePod(bookOut, pos + lutOffset + lutSize);
  }
  // Total size of the spine records: entries land in book.bin after the toc LUT
  // and the full spine block, so toc LUT positions are offset by it.
  const auto spineBytes = static_cast<uint32_t>(spineIn.position() - source.spineStart);

  // Loop through toc entries, writing LUT positions
  tocIn.seek(0);
  for (int i = 0; i < tocs; i++) {
    if (stopped(i)) return giveUp();
    const uint32_t pos = tocIn.position();
    readTocEntryFrom(tocIn);
    serialization::writePod(bookOut, pos + lutOffset + lutSize + spineBytes);
  }
  spineIn.seek(source.spineStart);
  if (resumable && !checkpoint(0, 0, -1)) return abandon();
  }

#ifdef TENOR_PRESS_PROBE
  const unsigned long lutsMs = millis() - lutsStarted;
#endif
  // LUTs complete. Spine entries are written in chunks sized from free heap so
  // the working set stays bounded at any chapter count. A book that fits one
  // chunk takes exactly the passes it always took: one TOC scan for the
  // spine->TOC mapping and one zip central-directory scan for the sizes.
  const bool useBatchSizes = spines >= LARGE_SPINE_THRESHOLD;
  int chunk = spines;
  if (useBatchSizes) {
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t budget = freeHeap > BOOKBIN_HEAP_RESERVE ? freeHeap - BOOKBIN_HEAP_RESERVE : 0;
    chunk = static_cast<int>(std::min<uint32_t>(spines, budget / BOOKBIN_ITEM_BYTES));
  }
#ifdef TENOR_PRESS_PROBE
  LOG_INF("BMC", "BOOKBIN spines=%d toc=%d chunk=%d free=%u largest=%u min=%u", spines, tocs, chunk,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          static_cast<unsigned>(ESP.getMinFreeHeap()));
#endif

  ZipFile zip(epubPath);
  // Pre-open zip file to speed up size calculations
  if (chunk < std::min<int>(spines, BOOKBIN_MIN_CHUNK) || !zip.open()) {
    LOG_ERR("BMC", "Could not size spine items: chunk %d, heap %u", chunk, static_cast<unsigned>(ESP.getFreeHeap()));
    return abandon();
  }
  // NOTE: We intentionally skip calling loadAllFileStatSlims() here.
  // For large EPUBs (2000+ chapters), pre-loading all ZIP central directory entries
  // into memory causes OOM crashes on ESP32-C3's limited ~380KB RAM.
  // Instead, for large books we use a batch lookup that scans the ZIP
  // central directory once per chunk and matches against spine targets using hash comparison.
  // This is O(n*log(m)) instead of O(n*m) while avoiding memory exhaustion.
  // See: https://github.com/crosspoint-reader/crosspoint-reader/issues/134

  const auto targetLess = [](const ZipFile::SizeTarget& a, const ZipFile::SizeTarget& b) {
    return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
  };
  uint32_t cumSize = ckpt.cumSize;
  int lastSpineTocIndex = ckpt.lastSpineTocIndex;
  spineIn.seek(resuming ? ckpt.spineInPos : source.spineStart);
  for (int first = ckpt.spinesDone; first < spines; first += chunk) {
    const int count = std::min(chunk, spines - first);
#ifdef TENOR_PRESS_PROBE
    chunks++;
    unsigned long stepStarted = millis();
#endif

    // Zip lookup targets of this chunk, by the hash of each item's path. They also match the
    // unmatched TOC entries of a background build to their chapters.
    std::deque<ZipFile::SizeTarget> targets(useBatchSizes ? count : 0);
    const size_t chunkStart = spineIn.position();
    if (useBatchSizes) {
      for (int i = 0; i < count; i++) {
        const std::string path = FsHelpers::normalisePath(readSpineEntryFrom(spineIn).href);
        targets[i] = {ZipFile::fnvHash64(path.c_str(), path.size()), static_cast<uint16_t>(path.size()),
                      static_cast<uint16_t>(i)};
      }
      std::sort(targets.begin(), targets.end(), targetLess);
      spineIn.seek(chunkStart);
    }

    // First TOC entry of each spine item in this chunk, in one TOC pass.
    std::deque<int16_t> spineToTocIndex(count, -1);
    tocIn.seek(0);
    for (int j = 0; j < tocs; j++) {
      if (stopped(j)) return giveUp();
      const TocEntry tocEntry = readTocEntryFrom(tocIn);
      int spineIndex = tocEntry.spineIndex;
      if (tocSpine) {
        if (tocSpine[j] < 0 && !targets.empty()) {
          // The first chapter with this path, as the in-memory index of the one-pass build chose.
          const ZipFile::SizeTarget key{ZipFile::fnvHash64(tocEntry.href.data(), tocEntry.href.size()),
                                        static_cast<uint16_t>(tocEntry.href.size()), 0};
          int found = -1;
          for (auto it = std::lower_bound(targets.begin(), targets.end(), key, targetLess);
               it != targets.end() && it->hash == key.hash && it->len == key.len; ++it) {
            if (found < 0 || it->index < found) found = it->index;
          }
          if (found >= 0) tocSpine[j] = static_cast<int16_t>(first + found);
        }
        spineIndex = tocSpine[j];
      }
      const int local = spineIndex - first;
      if (local >= 0 && local < count && spineToTocIndex[local] == -1) {
        spineToTocIndex[local] = static_cast<int16_t>(j);
      }
    }

#ifdef TENOR_PRESS_PROBE
    tocScanMs += millis() - stepStarted;
    stepStarted = millis();
#endif
    std::deque<uint32_t> spineSizes(count, 0);
    if (useBatchSizes) {
      const int matched = zip.fillUncompressedSizes(targets, spineSizes);
      LOG_DBG("BMC", "Batch lookup matched %d/%d spine items", matched, count);
      (void)matched;
    }
    if (stop && stop()) return giveUp();
#ifdef TENOR_PRESS_PROBE
    zipSizeMs += millis() - stepStarted;
    stepStarted = millis();
#endif

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
#ifdef TENOR_PRESS_PROBE
    spineOutMs += millis() - stepStarted;
#endif
    if (resumable && !checkpoint(first + count, cumSize, lastSpineTocIndex)) return abandon();
  }
  // Close opened zip file
  zip.close();

#ifdef TENOR_PRESS_PROBE
  const unsigned long tocCopyStarted = millis();
#endif
  // Loop through toc entries from toc file writing to book.bin
  tocIn.seek(0);
  for (int i = 0; i < tocs; i++) {
    if (stopped(i)) return giveUp();
    auto tocEntry = readTocEntryFrom(tocIn);
    if (tocSpine) tocEntry.spineIndex = tocSpine[i];
    writeTocEntryTo(bookOut, tocEntry);
  }

  const bool written = bookOut.flush() && spineIn.ok() && tocIn.ok();
#ifdef TENOR_PRESS_PROBE
  LOG_INF("BMC", "INDEX_SPLIT book luts=%lu chunks=%d toc_scans=%lu zip_sizes=%lu spine_out=%lu toc_copy=%lu", lutsMs,
          chunks, tocScanMs, zipSizeMs, spineOutMs, millis() - tocCopyStarted);
#endif

  const bool closed = outFile.close();
  spineSrc.close();
  tocSrc.close();

  if (!written || !closed) {
    // A short write (card full/removed) would leave a truncated book.bin that
    // still passes the version check on load; remove it so the next open rebuilds.
    LOG_ERR("BMC", "Failed writing %s, removing truncated file", source.outFile);
    Storage.remove(outPath.c_str());
    return false;
  }

  if (resumable) Storage.remove((cachePath + bookCkptFile).c_str());
  LOG_DBG("BMC", "Successfully built book.bin");
  return true;
}

bool BookMetadataCache::writePart(const BookMetadata& metadata, const TocSource& source) {
  // Leftovers of an earlier background build belong to another chapter list, and a book.bin this
  // build could not load (another cache version, damaged) would stand in front of book.part.
  removePartFiles();
  Storage.remove((cachePath + bookBinFile).c_str());
  const std::string partPath = cachePath + tmpBookPartFile;
  HalFile out, spines;
  if (!Storage.openFileForWrite("BMC", partPath, out)) return false;
  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spines)) {
    out.close();
    Storage.remove(partPath.c_str());
    return false;
  }
  bool ok = true;
  {
    serialization::BufferedFileWriter partOut(out, BUILD_IO_BUFFER_SIZE);
    serialization::BufferedFileReader spineBuffer(spines, BUILD_IO_BUFFER_SIZE);
    MetadataReader spineIn(spineBuffer, spines.size());
    const std::string* strings[] = {&metadata.title,         &metadata.author,  &metadata.language,
                                    &metadata.coverItemHref, &metadata.textReferenceHref,
                                    &source.ncxItem,         &source.navItem,   &source.basePath};
    uint32_t partLutOffset = sizeof(BOOK_PART_VERSION) + sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount);
    for (const auto* text : strings) partLutOffset += sizeof(uint32_t) + text->size();
    const uint32_t lutSize = sizeof(uint32_t) * spineCount;
    const uint16_t noToc = 0;
    serialization::writePod(partOut, BOOK_PART_VERSION);
    serialization::writePod(partOut, partLutOffset);
    serialization::writePod(partOut, spineCount);
    serialization::writePod(partOut, noToc);
    for (const auto* text : strings) serialization::writeString(partOut, *text);
    for (int i = 0; i < spineCount; i++) {
      const uint32_t pos = spineIn.position();
      readSpineEntryFrom(spineIn);
      serialization::writePod(partOut, pos + partLutOffset + lutSize);
    }
    ok = spineIn.ok() && spineIn.position() == spines.size() && spines.seek(0);
    // The records go across as they are: book.part has the spine pass's record layout.
    uint8_t block[512];
    for (size_t left = spines.size(); ok && left > 0;) {
      const size_t n = std::min(left, sizeof(block));
      ok = spines.read(block, n) == static_cast<int>(n);
      if (ok) partOut.write(block, n);
      left -= n;
    }
    ok = partOut.flush() && ok;
  }
  spines.close();
  ok = out.close() && ok;
  ok = ok && Storage.rename(partPath.c_str(), (cachePath + bookPartFile).c_str());
  if (!ok) {
    LOG_ERR("BMC", "Failed writing book.part");
    Storage.remove(partPath.c_str());
    return false;
  }
  Storage.remove((cachePath + tmpSpineBinFile).c_str());
  return true;
}

bool BookMetadataCache::beginDeferredTocPass() {
  Storage.remove((cachePath + tocCountFile).c_str());
  Storage.remove((cachePath + bookCkptFile).c_str());
  if (!Storage.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) return false;
  buildMode = true;
  deferTocMatch = true;
  tocCount = 0;
  // Wrapper OOM is fine: createTocEntry falls back to unbuffered writes.
  passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(tocFile, BUILD_IO_BUFFER_SIZE);
  return true;
}

bool BookMetadataCache::endDeferredTocPass() {
#ifdef TENOR_PRESS_PROBE
  const unsigned long flushStarted = millis();
#endif
  bool ok = !passOut || passOut->flush();
#ifdef TENOR_PRESS_PROBE
  logPassWrites("toc_bg", passOut.get(), millis() - flushStarted);
#endif
  passOut.reset();
  ok = tocFile.close() && ok;
  buildMode = false;
  deferTocMatch = false;
  const std::string tmpPath = cachePath + tmpTocBinFile;
  const std::string donePath = cachePath + tocDoneFile;
  Storage.remove(donePath.c_str());
  ok = ok && Storage.rename(tmpPath.c_str(), donePath.c_str());
  // Three bytes in one write, checked by size when read: a cut write is never taken for a count.
  HalFile countFile;
  const std::string countPath = cachePath + tocCountFile;
  const uint8_t record[3] = {TOC_COUNT_VERSION, static_cast<uint8_t>(tocCount), static_cast<uint8_t>(tocCount >> 8)};
  ok = ok && Storage.openFileForWrite("BMC", countPath, countFile);
  ok = ok && countFile.write(record, sizeof(record)) == sizeof(record);
  if (countFile) ok = countFile.close() && ok;
  if (!ok) {
    LOG_ERR("BMC", "Failed writing toc.bin");
    Storage.remove(tmpPath.c_str());
    Storage.remove(countPath.c_str());
  }
  return ok;
}

bool BookMetadataCache::deferredTocReady(const std::string& cachePath, uint16_t* entries) {
  HalFile countFile;
  if (!Storage.openFileForRead("BMC", cachePath + tocCountFile, countFile)) return false;
  uint8_t record[3] = {};
  const bool ok = countFile.size() == sizeof(record) && countFile.read(record, sizeof(record)) == sizeof(record) &&
                  record[0] == TOC_COUNT_VERSION && record[2] <= INT16_MAX >> 8;
  const uint16_t count = static_cast<uint16_t>(record[1] | (record[2] << 8));
  countFile.close();
  if (!ok || !Storage.exists((cachePath + tocDoneFile).c_str())) return false;
  if (entries) *entries = count;
  return true;
}

bool BookMetadataCache::buildBookBinFromPart(const std::string& epubPath, const StopFn stop) {
  uint16_t tocEntries = 0;
  if (!loaded || !partial || !deferredTocReady(cachePath, &tocEntries)) return false;
  const uint32_t spineStart = lutOffset + sizeof(uint32_t) * spineCount;
  const std::string binPath = cachePath + bookBinFile;
  const std::string tmpPath = cachePath + tmpBookBinFile;
  if (!buildBookBinFrom(epubPath, coreMetadata,
                        {bookPartFile, spineStart, tocDoneFile, tmpBookBinFile, true, spineCount, tocEntries},
                        stop)) {
    return false;
  }
  Storage.remove(binPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), binPath.c_str())) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return true;
}

bool BookMetadataCache::indexOnCard(const std::string& cachePath) {
  return Storage.exists((cachePath + bookBinFile).c_str()) || Storage.exists((cachePath + bookPartFile).c_str());
}

void BookMetadataCache::discardBookBin() const { Storage.remove((cachePath + bookBinFile).c_str()); }

bool BookMetadataCache::partFilesLeft() const { return Storage.exists((cachePath + bookPartFile).c_str()); }

bool BookMetadataCache::bookBinReady() const { return Storage.exists((cachePath + bookBinFile).c_str()); }

void BookMetadataCache::removePartFiles() const {
  // book.part last: while it is on the card, Epub::load knows something is left to remove.
  for (const char* name : {tmpBookPartFile, tocDoneFile, tmpTocBinFile, tocCountFile, tmpBookBinFile, bookCkptFile,
                           tmpBookCkptFile, bookPartFile}) {
    const std::string path = cachePath + name;
    if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  }
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

#ifdef TENOR_PRESS_PROBE
  const uint32_t started = micros();
#endif
  const SpineEntry entry(href, 0, -1);
  if (passOut) {
    writeSpineEntryTo(*passOut, entry);
  } else {
    writeSpineEntry(spineFile, entry);
  }
  spineCount++;
#ifdef TENOR_PRESS_PROBE
  indexProbe.spineWriteUs += micros() - started;
#endif
}

void BookMetadataCache::createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                       const uint8_t level) {
  if (!buildMode || !tocFile || (!deferTocMatch && !spineFile)) {
    LOG_DBG("BMC", "createTocEntry called but not in build mode");
    return;
  }

#ifdef TENOR_PRESS_PROBE
  const uint32_t started = micros();
#endif
  int16_t spineIndex = -1;

  if (deferTocMatch) {
    // Matched to its chapter by buildBookBinFromPart.
  } else if (useSpineHrefIndex) {
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
#ifdef TENOR_PRESS_PROBE
  indexProbe.tocEntryUs += micros() - started;
#endif
}

/* ============= READING / LOADING FUNCTIONS ================ */

bool BookMetadataCache::load(const bool allowPartial, const StopFn stop) {
  // A book.bin this build cannot use (another cache version, cut short, damaged) must not hide
  // book.part: nothing writes over it until the background build's own book.bin is whole.
  if (loadFile(false, stop)) return true;
  return allowPartial && !(stop && stop()) && loadFile(true, stop);
}

bool BookMetadataCache::loadFile(const bool partFile, const StopFn stop) {
  loaded = false;
  partial = false;
  tocCursor.reset();
  cumulativeSizes.reset();
  itemSizes.reset();
  spineCount = tocCount = 0;
  if (bookFile) bookFile.close();
  if (!Storage.openFileForRead("BMC", cachePath + (partFile ? bookPartFile : bookBinFile), bookFile)) return false;
  partial = partFile;

  // `invalid` is false for a load given up at `stop`: nothing is wrong with the file then.
  const auto fail = [this](const bool invalid = true) {
    if (invalid) LOG_ERR("BMC", "Invalid or unreadable book.bin; rejecting cache");
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
  if (!header.pod(version) || version != (partial ? BOOK_PART_VERSION : BOOK_CACHE_VERSION) ||
      !header.pod(lutOffset) || !header.pod(spineCount) || !header.pod(tocCount))
    return fail();
  if (!header.string(coreMetadata.title) || !header.string(coreMetadata.author) ||
      !header.string(coreMetadata.language) || !header.string(coreMetadata.coverItemHref) ||
      !header.string(coreMetadata.textReferenceHref))
    return fail();
  if (partial && (tocCount != 0 || !header.string(tocSource.ncxItem) || !header.string(tocSource.navItem) ||
                  !header.string(tocSource.basePath)))
    return fail();

  // Indices on disk are signed 16-bit. Check arithmetic before any allocation.
  const uint32_t lutSize = (static_cast<uint32_t>(spineCount) + tocCount) * sizeof(uint32_t);
  if (spineCount == 0 || spineCount > INT16_MAX || tocCount > INT16_MAX || lutOffset != header.position() ||
      lutOffset > fileSize || lutSize > fileSize - lutOffset)
    return fail();

  // book.part has no chapter sizes: no table, and getCumulativeSize answers 0.
  if (!partial) {
    itemSizes = makeUniqueNoThrow<uint16_t[]>(spineCount);
    cumulativeSizes = makeUniqueNoThrow<uint32_t[]>(
        itemSizes ? (spineCount + CUMULATIVE_STRIDE - 1) / CUMULATIVE_STRIDE : spineCount);
    if (!cumulativeSizes) return fail();
  }

  // Two sequential streams validate every LUT pointer and record, including the
  // TOC tail. Fixed 4KB buffers avoid per-field SD reads; no whole-book allocation.
  HalFile lutFile;
  if (!Storage.openFileForRead("BMC", cachePath + (partial ? bookPartFile : bookBinFile), lutFile) ||
      !lutFile.seek(lutOffset) ||
      !bookFile.seek(lutOffset + lutSize))
    return fail();
  serialization::BufferedFileReader lutBuffer(lutFile, BUILD_IO_BUFFER_SIZE);
  serialization::BufferedFileReader dataBuffer(bookFile, BUILD_IO_BUFFER_SIZE);
  MetadataReader lut(lutBuffer, lutOffset + lutSize);
  MetadataReader data(dataBuffer, fileSize);
  uint32_t previous = 0;
  for (uint16_t i = 0; i < spineCount; ++i) {
    if (stop && (i & 255) == 0 && stop()) return fail(false);
    uint32_t offset = 0;
    if (!lut.pod(offset) || offset != data.position()) return fail();
    const auto entry = readSpineEntryFrom(data);
    if (!data.ok() || entry.tocIndex < -1 || entry.tocIndex >= static_cast<int>(tocCount) ||
        entry.cumulativeSize < previous)
      return fail();
    if (partial) continue;
    if (itemSizes && entry.cumulativeSize - previous > UINT16_MAX) {
      // First chapter of 64 KB or more: the book needs the flat table. Free the compact one first
      // so the two never share the heap (a load with the radio up has room for one), then read the
      // spine again from the top.
      itemSizes.reset();
      cumulativeSizes.reset();
      cumulativeSizes = makeUniqueNoThrow<uint32_t[]>(spineCount);
      if (!cumulativeSizes || !lut.seek(lutOffset) || !data.seek(lutOffset + lutSize)) return fail();
      previous = 0;
      i = UINT16_MAX;  // the loop increment wraps it to 0
      continue;
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
    if (stop && (i & 255) == 0 && stop()) return fail(false);
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
