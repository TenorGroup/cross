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
  // Asked between the steps of a long pass; true gives the pass up.
  using StopFn = bool (*)();

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
  std::string sourcePath;
  uint32_t lutOffset;
  uint16_t spineCount;
  uint16_t tocCount;
  bool loaded;
  bool buildMode;
  // Loaded from book.part: chapters and metadata only, no TOC and no chapter sizes.
  bool partial = false;
  // Set by beginDeferredTocPass: TOC entries are written unmatched, buildBookBinFromPart matches them.
  bool deferTocMatch = false;
  bool heapRefused = false;
  uint16_t deferredResumeCount = 0;
  uint32_t deferredSeen = 0;
  uint64_t deferredSourceKey = 0;

  HalFile bookFile;
  // Temp file handles during build
  HalFile spineFile;
  HalFile tocFile;
  // Buffers the per-entry tmp-file writes during the OPF/TOC passes: those
  // writes interleave with zip-inflate SD reads, and unbuffered they thrash
  // SdFat's shared sector cache (one 512B transaction per 4-byte pod). One
  // wrapper serves whichever pass is active (spine, then toc).
  std::unique_ptr<serialization::BufferedFileWriter> passOut;

  // Cumulative spine sizes. They stay resident beside the page-turner radio for the whole
  // session, so RAM holds only the running total before every CUMULATIVE_STRIDE-th item and the
  // book's total; the totals of one window of CUMULATIVE_STRIDE items are read from book.bin when
  // a lookup falls outside the window held. Page turns ask the book's total and the current
  // item's two totals, which that window and the stride table answer without the card.
  static constexpr uint16_t CUMULATIVE_STRIDE = 32;
  std::unique_ptr<uint32_t[]> strideStarts;  // total before item w * CUMULATIVE_STRIDE
  uint32_t bookSize = 0;
  mutable int windowFirst = -1;  // first item of `window`, -1 when it holds none
  mutable uint32_t window[CUMULATIVE_STRIDE] = {};
  // Reads the totals of the window starting at `first` into `window`. Called under the size lock.
  bool readSizeWindow(int first) const;

  // The hash narrows the lookup; the stored file offset verifies the full href.
  struct SpineHrefIndexEntry {
    uint32_t hashLow;
    uint16_t hashHigh;
    int16_t spineIndex;
    uint32_t offset = 0;
    bool operator<(const SpineHrefIndexEntry& o) const {
      return hashHigh < o.hashHigh ||
             (hashHigh == o.hashHigh &&
              (hashLow < o.hashLow || (hashLow == o.hashLow && spineIndex < o.spineIndex)));
    }
    bool sameKey(const SpineHrefIndexEntry& o) const { return hashHigh == o.hashHigh && hashLow == o.hashLow; }
  };
  static SpineHrefIndexEntry hrefKey(const std::string& href, const int16_t spineIndex) {
    const uint64_t hash = fnvHash64(href);
    return {static_cast<uint32_t>(hash), static_cast<uint16_t>(hash >> 32), spineIndex};
  }
  std::unique_ptr<SpineHrefIndexEntry[]> spineHrefIndex;
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

  // Where buildBookBinFrom reads and writes. The background build reads the chapters from book.part
  // and matches each TOC entry to its chapter itself (resolveToc).
  struct BookBinSource {
    const char* spineFile;
    uint32_t spineStart;
    const char* tocFile;
    const char* outFile;
    bool resolveToc;
    uint16_t spines;
    uint16_t tocs;
  };
  bool buildBookBinFrom(const std::string& epubPath, const BookMetadata& metadata, const BookBinSource& source,
                        StopFn stop);

  // load() of book.bin (partFile false) or book.part.
  bool loadFile(bool partFile, StopFn stop);

  uint32_t writeSpineEntry(HalFile& file, const SpineEntry& entry) const;
  uint32_t writeTocEntry(HalFile& file, const TocEntry& entry) const;
  SpineEntry readSpineEntry(HalFile& file) const;
  TocEntry readTocEntry(HalFile& file) const;

 public:
  BookMetadata coreMetadata;

  explicit BookMetadataCache(std::string cachePath, std::string sourcePath = {})
      : cachePath(std::move(cachePath)),
        sourcePath(std::move(sourcePath)),
        lutOffset(0),
        spineCount(0),
        tocCount(0),
        loaded(false),
        buildMode(false) {}
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

  // A book of this many chapters opens on its chapter list alone (book.part) and builds its TOC
  // and chapter sizes afterwards, a step at a time (Epub::indexSome). The one rule for which
  // books do that.
  static bool indexesInBackground(int spineCount) { return spineCount >= LARGE_SPINE_THRESHOLD; }

  // Where a book's TOC lives inside the EPUB, kept in book.part so a reopen can finish the index
  // without parsing content.opf again.
  struct TocSource {
    std::string ncxItem;
    std::string navItem;
    std::string basePath;
  };
  TocSource tocSource;

  // After endContentOpfPass: writes book.part from the spine pass (through a temporary file) and
  // removes the spine pass file.
  bool writePart(const BookMetadata& metadata, const TocSource& source);
  // The background TOC pass: entries are written without their chapter (matched later), so it
  // needs no spine index in memory. endDeferredTocPass keeps the finished pass under its own name
  // and records the entry count; an unfinished pass is never taken for a finished one.
  bool beginDeferredTocPass(const std::string& source = {}, bool restart = false);
  bool pauseDeferredTocPass();
  bool hasDeferredTocProgress(const std::string& source) const;
  bool endDeferredTocPass();
  // Whether a finished background TOC pass is on the card, and its entry count.
  static bool deferredTocReady(const std::string& cachePath, uint16_t* entries = nullptr);
  // THE heap decision before a background TOC step: true when the heap now (reported numbers)
  // stands on the floor after the step. `inflateBytes` is what the zip stream's inflater takes from
  // the heap, 0 when it rides the lent framebuffer. Besides it the step took 23,7 to 28 KB on the
  // X3 (heap59, 5.000 chapters: zip readers, two 1 KB slices, the pass and its 4 KB buffer, the
  // parser), 18 KB of it counted on the host (BackgroundIndex.TocStepStaysInItsBudget), in two
  // blocks of 4 KB. Every block from 4 KB up is booked against the largest free block.
  static constexpr size_t TOC_STEP_HEAP = 32 * 1024;
  static constexpr size_t TOC_STEP_BLOCKS = 16 * 1024;
  static bool tocStepFits(size_t inflateBytes, size_t freeNow, size_t largestNow);
  // Builds book.bin from book.part and the finished TOC pass. `stop` is asked between steps; when
  // it answers true, or on any failure, nothing of the build is left behind and false is returned.
  // Needs a cache loaded from book.part.
  bool buildBookBinFromPart(const std::string& epubPath, StopFn stop);
  bool wasHeapRefused() const { return heapRefused; }
  // Whether the card holds an index this cache can load (book.bin, or book.part).
  static bool indexOnCard(const std::string& cachePath);
  // Whether the background build has left book.bin on the card, not loaded yet.
  bool bookBinReady() const;
  // Removes book.part and the background pass files once book.bin is loaded.
  void removePartFiles() const;
  // Whether a power cut between the background build's last two steps left them on the card.
  bool partFilesLeft() const;
  // Removes a book.bin that would not load, so the background build writes it again.
  void discardBookBin() const;

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

  // Reading phase (read mode). With `allowPartial`, a missing book.bin falls back to book.part.
  // `stop` is asked while the records are checked; a stopped load fails.
  bool load(bool allowPartial = false, StopFn stop = nullptr);
  // Read the small metadata sidecar and stat book.bin without validating its records.
  bool loadMetadataOnly(BookMetadata& metadata, uint16_t* spine = nullptr, uint16_t* toc = nullptr,
                        uint32_t* lut = nullptr) const;
  bool isPartial() const { return partial; }
  SpineEntry getSpineEntry(int index);
  TocEntry getTocEntry(int index);
  // Cumulative byte size up to and including the given spine item (0 if out of range
  // or not loaded, or loaded from book.part).
  uint32_t getCumulativeSize(int index) const;
  // The first spine item whose cumulative size reaches `size`, -1 if none does. Reads at most one
  // window of sizes from the card.
  int getSpineIndexForSize(uint32_t size) const;
  int getSpineCount() const { return spineCount; }
  int getTocCount() const { return tocCount; }
  bool isLoaded() const { return loaded; }
};
