#pragma once

#include <BufferedFile.h>
#include <HalStorage.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#ifdef TENOR_PRESS_PROBE
// Microseconds a first-open index build spends in each part, printed as INDEX_SPLIT lines.
struct IndexProbe {
  uint32_t parseUs = 0;        // inside the XML parsers, their card work included
  uint32_t manifestIoUs = 0;   // OPF manifest items written to .items.bin
  uint32_t spineLookupUs = 0;  // spine idrefs looked up in .items.bin
  uint32_t spineWriteUs = 0;   // spine entries written to spine.bin.tmp
  uint32_t tocEntryUs = 0;     // TOC entries matched to the spine and written to toc.bin.tmp
  uint32_t manifestItems = 0;
};
extern IndexProbe indexProbe;
#endif

class BookMetadataCache {
 public:
  struct BookMetadata {
    std::string title;
    std::string author;
    std::string language;
    std::string coverItemHref;
    std::string textReferenceHref;
  };

  struct SpineEntry {
    std::string href;
    uint32_t cumulativeSize;
    int16_t tocIndex;

    SpineEntry() : cumulativeSize(0), tocIndex(-1) {}
    SpineEntry(std::string href, const uint32_t cumulativeSize, const int16_t tocIndex)
        : href(std::move(href)), cumulativeSize(cumulativeSize), tocIndex(tocIndex) {}
  };

  struct TocEntry {
    std::string title;
    std::string href;
    std::string anchor;
    uint8_t level;
    int16_t spineIndex;

    TocEntry() : level(0), spineIndex(-1) {}
    TocEntry(std::string title, std::string href, std::string anchor, const uint8_t level, const int16_t spineIndex)
        : title(std::move(title)),
          href(std::move(href)),
          anchor(std::move(anchor)),
          level(level),
          spineIndex(spineIndex) {}
  };

 private:
  std::string cachePath;
  uint32_t lutOffset;
  uint16_t spineCount;
  uint16_t tocCount;
  bool loaded;
  bool buildMode;

  HalFile bookFile;
  // Temp file handles during build
  HalFile spineFile;
  HalFile tocFile;
  // Buffers the per-entry tmp-file writes during the OPF/TOC passes: those
  // writes interleave with zip-inflate SD reads, and unbuffered they thrash
  // SdFat's shared sector cache (one 512B transaction per 4-byte pod). One
  // wrapper serves whichever pass is active (spine, then toc).
  std::unique_ptr<serialization::BufferedFileWriter> passOut;

  // Cumulative spine sizes, cached in RAM at load() so progress/percent lookups are
  // O(1) instead of 2 seeks + a heap-allocating SpineEntry read per access. The
  // table stays resident beside the page-turner radio for the whole session, so
  // chapter sizes that fit 16 bits are kept as two bytes each plus one exact
  // running total per CUMULATIVE_STRIDE items; a book with a chapter of 64 KB
  // or more keeps a flat table of four bytes per item.
  static constexpr uint16_t CUMULATIVE_STRIDE = 32;
  std::unique_ptr<uint32_t[]> cumulativeSizes;  // flat table, or one total per stride
  std::unique_ptr<uint16_t[]> itemSizes;        // per-item sizes; null for a flat table

  // Index for fast href→spineIndex lookup (used only for large EPUBs). The
  // key is the low 48 bits of the FNV-1a 64-bit hash: eight bytes per spine
  // item instead of sixteen, and still no expected collision at 32k items.
  struct SpineHrefIndexEntry {
    uint32_t hashLow;
    uint16_t hashHigh;
    int16_t spineIndex;
    bool operator<(const SpineHrefIndexEntry& o) const {
      return hashHigh < o.hashHigh || (hashHigh == o.hashHigh && hashLow < o.hashLow);
    }
    bool sameKey(const SpineHrefIndexEntry& o) const { return hashHigh == o.hashHigh && hashLow == o.hashLow; }
  };
  static SpineHrefIndexEntry hrefKey(const std::string& href, const int16_t spineIndex) {
    const uint64_t hash = fnvHash64(href);
    return {static_cast<uint32_t>(hash), static_cast<uint16_t>(hash >> 32), spineIndex};
  }
  std::deque<SpineHrefIndexEntry> spineHrefIndex;
  bool useSpineHrefIndex = false;

  static constexpr uint16_t LARGE_SPINE_THRESHOLD = 400;

  // FNV-1a 64-bit hash function
  static uint64_t fnvHash64(const std::string& s) {
    uint64_t hash = 14695981039346656037ull;
    for (char c : s) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 1099511628211ull;
    }
    return hash;
  }

  uint32_t writeSpineEntry(HalFile& file, const SpineEntry& entry) const;
  uint32_t writeTocEntry(HalFile& file, const TocEntry& entry) const;
  SpineEntry readSpineEntry(HalFile& file) const;
  TocEntry readTocEntry(HalFile& file) const;

 public:
  BookMetadata coreMetadata;

  explicit BookMetadataCache(std::string cachePath)
      : cachePath(std::move(cachePath)), lutOffset(0), spineCount(0), tocCount(0), loaded(false), buildMode(false) {}
  ~BookMetadataCache() = default;

  // Building phase (stream to disk immediately)
  bool beginWrite();
  bool beginContentOpfPass();
  void createSpineEntry(const std::string& href);
  bool endContentOpfPass();
  bool beginTocPass();
  void createTocEntry(const std::string& title, const std::string& href, const std::string& anchor, uint8_t level);
  bool endTocPass();
  bool endWrite();
  bool cleanupTmpFiles() const;

  // Post-processing to update mappings and sizes
  bool buildBookBin(const std::string& epubPath, const BookMetadata& metadata);

  // Independent sequential stream: random lookups cannot disturb its position.
  // One 2 KB buffer exists for the lifetime of the cursor.
  class TocCursor {
   public:
    TocCursor(const std::string& path, uint32_t lut, int start, uint16_t spine, uint16_t toc);
    bool next(TocEntry& entry);
    bool seekTo(int target);
    bool failed() const { return error; }

   private:
    HalFile file;
    std::unique_ptr<serialization::BufferedFileReader> stream;
    size_t end = 0;
    uint32_t lutOffset = 0;
    static constexpr uint16_t LOOKUP_WINDOW = 32;
    uint32_t offsets[LOOKUP_WINDOW] = {};
    int windowStart = -1;
    uint16_t windowCount = 0;
    uint16_t index = 0, spineCount = 0, tocCount = 0;
    bool error = true;
  };

 private:
  // Reuse one buffered stream and a bounded LUT window for repeated, ascending,
  // and descending lookups. Each record still passes the checked reader.
  std::unique_ptr<TocCursor> tocCursor;

 public:
  std::unique_ptr<TocCursor> openTocCursor(int start = 0) const;

  // Reading phase (read mode)
  bool load();
  SpineEntry getSpineEntry(int index);
  TocEntry getTocEntry(int index);
  // Cumulative byte size up to and including the given spine item (0 if out of range
  // or not loaded). Backed by the in-RAM cumulativeSizes cache populated in load().
  uint32_t getCumulativeSize(int index) const;
  int getSpineCount() const { return spineCount; }
  int getTocCount() const { return tocCount; }
  bool isLoaded() const { return loaded; }
};
