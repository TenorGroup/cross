// Indexing a book with thousands of chapters under a device-sized heap.
// Drives the real OPF, nav and book.bin passes in the order Epub::load runs
// them, over an in-memory card, and counts every heap byte they allocate.
#include "HeapCap.h"

#include <BookMetadataCache.h>
#include <Epub/parsers/ContentOpfParser.h>
#include <Epub/parsers/TocNavParser.h>
#include <ZipFile.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <string>

namespace {
const std::string cachePath = "/cache/book";
const std::string basePath = "OEBPS/";
const std::string epubPath = "/books/huge.epub";

// Heap left for indexing when a book is opened on the X3: the reader enters
// with ~116 KB free and a 90 KB largest block while the radio is paused.
constexpr size_t OPEN_HEAP = 110 * 1024;
// Heap left while the page-turner radio runs (sleep screen, home cover).
constexpr size_t RADIO_HEAP = 45 * 1024;

enum class Kind { Split, OneFile };

std::string pad5(int i) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%05d", i);
  return buf;
}

// One chapter can be made oversized (64 KB or more) to cover the flat table.
int bigChapter = 0;
uint32_t chapterBytes(int i) { return i == bigChapter ? 300000 : 8000 + static_cast<uint32_t>(i * 37 % 9000); }

struct Book {
  std::string opf, nav;
  std::vector<std::pair<std::string, uint32_t>> zip;
};

Book makeBook(Kind kind, int n) {
  heapcap::Untracked guard;
  Book b;
  std::string manifest =
      "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
      "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>";
  std::string spine, items;
  b.zip.push_back({"mimetype", 20});
  b.zip.push_back({"META-INF/container.xml", 200});
  if (kind == Kind::Split) {
    for (int i = 1; i <= n; ++i) {
      const std::string file = "c" + pad5(i) + ".xhtml";
      manifest += "<item id=\"c" + std::to_string(i) + "\" href=\"" + file +
                  "\" media-type=\"application/xhtml+xml\"/>";
      spine += "<itemref idref=\"c" + std::to_string(i) + "\"/>";
      items += "<li><a href=\"" + file + "\">Chương " + std::to_string(i) + ": Mưa nắng</a></li>";
      b.zip.push_back({basePath + file, chapterBytes(i)});
    }
  } else {
    manifest += "<item id=\"mot\" href=\"mot.xhtml\" media-type=\"application/xhtml+xml\"/>";
    spine = "<itemref idref=\"mot\"/>";
    uint32_t total = 0;
    for (int i = 1; i <= n; ++i) {
      items += "<li><a href=\"mot.xhtml#c" + pad5(i) + "\">Chương " + std::to_string(i) + ": Mưa nắng</a></li>";
      total += chapterBytes(i);
    }
    b.zip.push_back({basePath + "mot.xhtml", total});
  }
  b.zip.push_back({basePath + "nav.xhtml", 400});
  b.zip.push_back({basePath + "toc.ncx", 400});
  b.zip.push_back({basePath + "content.opf", 400});
  b.opf =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?><package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>Sách thử</dc:title>"
      "<dc:creator>Tác giả thử</dc:creator><dc:language>vi</dc:language></metadata><manifest>" +
      manifest + "</manifest><spine toc=\"ncx\">" + spine + "</spine></package>";
  b.nav =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?><html xmlns=\"http://www.w3.org/1999/xhtml\" "
      "xmlns:epub=\"http://www.idpf.org/2007/ops\"><head><title>Mục lục</title></head><body>"
      "<nav epub:type=\"toc\"><ol>" +
      items + "</ol></nav></body></html>";
  return b;
}

template <typename P>
bool feed(P& parser, const std::string& xml) {
  // Same 1 KB slices the zip inflater hands the parsers on the device.
  for (size_t at = 0; at < xml.size(); at += 1024) {
    const size_t n = std::min<size_t>(1024, xml.size() - at);
    if (parser.write(reinterpret_cast<const uint8_t*>(xml.data() + at), n) != n) return false;
  }
  return true;
}

struct IndexRun {
  bool ok = false;
  const char* failedAt = "";
  size_t peakOpf = 0, peakToc = 0, peakBook = 0, peakLoad = 0;
  unsigned aborts = 0;
  const char* abortPhase = "";
  size_t abortSize = 0, abortLive = 0;
  double ms = 0;
  size_t zipScanned = 0, zipScans = 0;
};

// Mirrors the cache-building half of Epub::load.
IndexRun indexBook(const Book& book, size_t cap) {
  {
    heapcap::Untracked guard;
    Storage.files.clear();
    zipModel = {};
    zipModel.entries = book.zip;
  }
  IndexRun run;
  const auto started = std::chrono::steady_clock::now();
  heapcap::reset(cap);
  const char* phase = "opf";
  auto mark = [&](size_t& peak) {
    peak = heapcap::peak;
    if (heapcap::aborts && !*run.abortPhase) {
      run.abortPhase = phase;
      run.abortSize = heapcap::firstAbortSize;
      run.abortLive = heapcap::firstAbortLive;
    }
    heapcap::peak = heapcap::live;
  };
  auto fail = [&](const char* where) {
    run.failedAt = where;
    run.aborts = heapcap::aborts;
    heapcap::stop();
    return run;
  };
  BookMetadataCache::BookMetadata metadata;
  {
    auto cache = std::make_unique<BookMetadataCache>(cachePath);
    if (!cache->beginWrite() || !cache->beginContentOpfPass()) return fail("begin");
    {
      ContentOpfParser opf(cachePath, basePath, book.opf.size(), cache.get());
      if (!opf.setup() || !feed(opf, book.opf)) {
        mark(run.peakOpf);
        return fail("opf");
      }
      metadata.title = opf.title;
    }
    if (!cache->endContentOpfPass()) return fail("opf-end");
    mark(run.peakOpf);
    phase = "toc";
    if (!cache->beginTocPass()) {
      mark(run.peakToc);
      return fail("toc-begin");
    }
    {
      TocNavParser nav(basePath, book.nav.size(), cache.get());
      if (!nav.setup() || !feed(nav, book.nav)) {
        mark(run.peakToc);
        return fail("toc");
      }
    }
    if (!cache->endTocPass() || !cache->endWrite()) return fail("toc-end");
    mark(run.peakToc);
    phase = "bookbin";
    const bool built = cache->buildBookBin(epubPath, metadata);
    mark(run.peakBook);
    if (!built) return fail("bookbin");
    cache->cleanupTmpFiles();
  }
  phase = "load";
  BookMetadataCache loaded(cachePath);
  const bool ok = loaded.load();
  mark(run.peakLoad);
  run.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  run.aborts = heapcap::aborts;
  run.zipScanned = zipModel.scannedEntries;
  run.zipScans = zipModel.scans;
  heapcap::stop();
  if (!ok) return fail("load");
  run.ok = true;
  return run;
}

void print(const char* label, int n, size_t cap, const IndexRun& r) {
  printf("HUGE_INDEX %s n=%d cap=%zu ok=%d failed_at=%s aborts=%u abort_phase=%s abort_request=%zu abort_live=%zu "
         "peak_opf=%zu peak_toc=%zu peak_bookbin=%zu peak_load=%zu zip_scans=%zu zip_entries=%zu ms=%.1f\n",
         label, n, cap == SIZE_MAX ? 0 : cap, r.ok ? 1 : 0, r.failedAt, r.aborts, r.abortPhase, r.abortSize,
         r.abortLive, r.peakOpf, r.peakToc, r.peakBook, r.peakLoad, r.zipScans, r.zipScanned, r.ms);
}

// Every spine entry must carry its own inflated size and the first TOC entry
// that points at it.
void expectSplitCache(int n) {
  BookMetadataCache cache(cachePath);
  ASSERT_TRUE(cache.load());
  ASSERT_EQ(cache.getSpineCount(), n);
  ASSERT_EQ(cache.getTocCount(), n);
  uint32_t total = 0;
  for (int i = 0; i < n; ++i) {
    total += chapterBytes(i + 1);
    ASSERT_EQ(cache.getCumulativeSize(i), total) << "spine " << i;
  }
  for (int i : {0, 1, n / 2, n - 2, n - 1}) {
    const auto spine = cache.getSpineEntry(i);
    EXPECT_EQ(spine.href, basePath + "c" + pad5(i + 1) + ".xhtml");
    EXPECT_EQ(spine.tocIndex, i);
    EXPECT_EQ(cache.getTocEntry(i).spineIndex, i);
  }
}

void expectOneFileCache(int n) {
  BookMetadataCache cache(cachePath);
  ASSERT_TRUE(cache.load());
  ASSERT_EQ(cache.getSpineCount(), 1);
  ASSERT_EQ(cache.getTocCount(), n);
  EXPECT_EQ(cache.getSpineEntry(0).tocIndex, 0);
  const auto last = cache.getTocEntry(n - 1);
  EXPECT_EQ(last.spineIndex, 0);
  EXPECT_EQ(last.anchor, "c" + pad5(n));
}
}  // namespace

// Step 1 of the investigation: how each pass scales with the chapter count.
TEST(HugeBookIndex, PeakHeapByChapterCount) {
  for (int n : {400, 1000, 2000, 3000, 5000, 8000}) {
    const Book book = makeBook(Kind::Split, n);
    print("split", n, SIZE_MAX, indexBook(book, SIZE_MAX));
  }
  for (int n : {2000, 5000}) {
    const Book book = makeBook(Kind::OneFile, n);
    print("onefile", n, SIZE_MAX, indexBook(book, SIZE_MAX));
  }
}

TEST(HugeBookIndex, SplitFiveThousandFitsOpenHeap) {
  const Book book = makeBook(Kind::Split, 5000);
  const IndexRun r = indexBook(book, OPEN_HEAP);
  print("split", 5000, OPEN_HEAP, r);
  EXPECT_EQ(r.aborts, 0u) << "device would abort in " << r.abortPhase;
  ASSERT_TRUE(r.ok) << r.failedAt;
  expectSplitCache(5000);
}

TEST(HugeBookIndex, OneFileFiveThousandFitsOpenHeap) {
  const Book book = makeBook(Kind::OneFile, 5000);
  const IndexRun r = indexBook(book, OPEN_HEAP);
  print("onefile", 5000, OPEN_HEAP, r);
  EXPECT_EQ(r.aborts, 0u) << "device would abort in " << r.abortPhase;
  ASSERT_TRUE(r.ok) << r.failedAt;
  expectOneFileCache(5000);
}

// With the radio running there is not enough heap for every index; the build
// must then finish or refuse the book, never abort.
TEST(HugeBookIndex, SplitFiveThousandUnderRadioHeapNeverAborts) {
  const Book book = makeBook(Kind::Split, 5000);
  const IndexRun r = indexBook(book, RADIO_HEAP);
  print("split", 5000, RADIO_HEAP, r);
  EXPECT_EQ(r.aborts, 0u) << "device would abort in " << r.abortPhase;
  if (r.ok) expectSplitCache(5000);
}

// Past what the open heap can hold the book is refused cleanly.
TEST(HugeBookIndex, OversizedBookIsRefusedWithoutAbort) {
  const Book book = makeBook(Kind::Split, 20000);
  const IndexRun r = indexBook(book, OPEN_HEAP);
  print("split", 20000, OPEN_HEAP, r);
  EXPECT_EQ(r.aborts, 0u) << "device would abort in " << r.abortPhase;
  if (r.ok) expectSplitCache(20000);
}

// The chunked book.bin pass must not cost ordinary books anything: a book
// that fits one chunk still scans the zip directory once.
TEST(HugeBookIndex, OrdinaryBooksKeepOneDirectoryScan) {
  for (int n : {300, 2000}) {
    const Book book = makeBook(Kind::Split, n);
    const IndexRun r = indexBook(book, OPEN_HEAP);
    print("split", n, OPEN_HEAP, r);
    EXPECT_EQ(r.aborts, 0u);
    ASSERT_TRUE(r.ok) << r.failedAt;
    EXPECT_LE(r.zipScans, 1u);
    expectSplitCache(n);
  }
}

// The size table stays resident for the whole reading session, next to the
// page-turner radio. X3 r45b: reading a 2,000-chapter book with the radio on
// already bottomed out at 23 KB free.
TEST(HugeBookIndex, LoadedSizeTableStaysSmall) {
  for (int big : {0, 777}) {
    bigChapter = big;
    const Book book = makeBook(Kind::Split, 5000);
    ASSERT_TRUE(indexBook(book, SIZE_MAX).ok);
    heapcap::reset(SIZE_MAX);
    size_t resident = 0, peak = 0;
    {
      BookMetadataCache cache(cachePath);
      ASSERT_TRUE(cache.load());
      resident = heapcap::live;
      peak = heapcap::peak;
      heapcap::stop();
      uint32_t total = 0;
      for (int i = 0; i < 5000; ++i) {
        total += chapterBytes(i + 1);
        ASSERT_EQ(cache.getCumulativeSize(i), total) << "spine " << i;
      }
    }
    printf("HUGE_INDEX resident n=5000 big_chapter=%d bytes=%zu peak=%zu\n", big, resident, peak);
    // Two bytes per chapter plus one running total per 32; a book with a
    // chapter of 64 KB or more keeps four bytes per chapter.
    EXPECT_LE(resident, big ? 5000u * 4 + 4096 : 5000u * 2 + 5000u / 8 + 4096);
    // Load may also hold its two 4 KB read buffers, never both tables at once:
    // the radio heap has room for one flat table, not for a flat table on top
    // of the compact one.
    EXPECT_LE(peak, (big ? 5000u * 4 : 5000u * 2 + 5000u / 8) + 2 * 4096 + 4096);
  }
  bigChapter = 0;
}

// The largest split book the open heap indexes, found by bisection. Every
// size tried must either index or be refused; none may abort.
TEST(HugeBookIndex, LargestBookThatFitsOpenHeap) {
  int fits = 0, refused = 32000;
  while (refused - fits > 250) {
    const int n = (fits + refused) / 2;
    const IndexRun r = indexBook(makeBook(Kind::Split, n), OPEN_HEAP);
    ASSERT_EQ(r.aborts, 0u) << "n=" << n << " would abort in " << r.abortPhase;
    (r.ok ? fits : refused) = n;
  }
  printf("HUGE_INDEX largest_split_open_heap fits=%d refused=%d\n", fits, refused);
  EXPECT_GE(fits, 5000);
}

// The OPF pass writes the manifest to .items.bin and looks every spine item up in it, between zip
// reads sharing the card's one sector cache. X3 r08 (5.000 chapters): 0,7 s of manifest writes and
// 0,9 s of lookups, one small card call per field. Both now go through buffers.
TEST(HugeBookIndex, OpfPassReachesTheCardInBlocks) {
  const Book book = makeBook(Kind::Split, 5000);
  {
    heapcap::Untracked guard;
    Storage.files.clear();
  }
  cardCalls = {};
  {
    BookMetadataCache cache(cachePath);
    ASSERT_TRUE(cache.beginWrite() && cache.beginContentOpfPass());
    {
      ContentOpfParser opf(cachePath, basePath, book.opf.size(), &cache);
      ASSERT_TRUE(opf.setup() && feed(opf, book.opf));
    }
    ASSERT_TRUE(cache.endContentOpfPass());
    ASSERT_EQ(cache.getSpineCount(), 5000);
  }
  printf("HUGE_INDEX opf_card_calls n=5000 reads=%zu writes=%zu seeks=%zu\n", cardCalls.reads, cardCalls.writes,
         cardCalls.seeks);
  // One card call per 1-4 KB block, not per field: 5.000 items used to take 20.000 writes and
  // 25.000 reads and seeks.
  EXPECT_LT(cardCalls.writes, 1000u);
  EXPECT_LT(cardCalls.reads, 1500u);
  EXPECT_LT(cardCalls.seeks, 1500u);
}

// ---------------------------------------------------------------------------
// Background index: a book of thousands of chapters opens on its chapter list
// (book.part) and builds the TOC and chapter sizes afterwards, a step at a
// time (Epub::load, Epub::indexSome). Same card, same parsers, driven in the
// order those two run them.
namespace {
const std::string partPath = cachePath + "/book.part";
const std::string binPath = cachePath + "/book.bin";

void resetCard(const Book& book) {
  heapcap::Untracked guard;
  Storage.files.clear();
  zipModel = {};
  zipModel.entries = book.zip;
}

std::vector<uint8_t> fileBytes(const std::string& path) {
  heapcap::Untracked guard;
  const auto it = Storage.files.find(path);
  return it == Storage.files.end() ? std::vector<uint8_t>{} : it->second->bytes;
}

// Epub::load's build path for a large book: the OPF pass, then book.part.
bool writeChapterList(const Book& book) {
  BookMetadataCache::BookMetadata metadata;
  BookMetadataCache cache(cachePath);
  if (!cache.beginWrite() || !cache.beginContentOpfPass()) return false;
  {
    ContentOpfParser opf(cachePath, basePath, book.opf.size(), &cache);
    if (!opf.setup() || !feed(opf, book.opf)) return false;
    metadata.title = opf.title;
  }
  if (!cache.endContentOpfPass() || !BookMetadataCache::indexesInBackground(cache.getSpineCount())) return false;
  cache.endWrite();
  return cache.writePart(metadata, {"", basePath + "nav.xhtml", basePath});
}

// indexSome's TOC step. Without `finish` the pass is cut the way a power cut or a stop cuts it.
bool tocStep(const Book& book, const bool finish = true) {
  BookMetadataCache pass(cachePath);
  if (!pass.beginDeferredTocPass()) return false;
  TocNavParser nav(basePath, book.nav.size(), &pass);
  if (!nav.setup() || !feed(nav, book.nav)) return false;
  return !finish || pass.endDeferredTocPass();
}

// indexSome's book.bin step, from a cache loaded the way the reader holds it.
bool bookStep(const BookMetadataCache::StopFn stop = nullptr) {
  BookMetadataCache part(cachePath);
  return part.load(/*allowPartial=*/true) && part.buildBookBinFromPart(epubPath, stop);
}

// A stop question that answers true from its `stopAfter`-th call on.
int stopAfter = 0, stopAsked = 0;
bool stopCounter() { return ++stopAsked >= stopAfter; }
bool neverStop() { return false; }

bool indexInBackground(const Book& book) {
  resetCard(book);
  return writeChapterList(book) && tocStep(book) && bookStep();
}

std::vector<uint8_t> onePassBookBin(const Book& book) {
  EXPECT_TRUE(indexBook(book, SIZE_MAX).ok);
  return fileBytes(binPath);
}
}  // namespace

TEST(BackgroundIndex, OnlyBooksOfFourHundredChaptersOrMore) {
  EXPECT_FALSE(BookMetadataCache::indexesInBackground(30));
  EXPECT_FALSE(BookMetadataCache::indexesInBackground(399));
  EXPECT_TRUE(BookMetadataCache::indexesInBackground(400));
  EXPECT_TRUE(BookMetadataCache::indexesInBackground(5000));
}

// The first page needs the chapter list and the book's metadata, nothing more: no zip
// directory scan for chapter sizes, no TOC. Every chapter can be opened from book.part.
TEST(BackgroundIndex, ChapterListIsReadableBeforeTheTocAndSizes) {
  const int n = 5000;
  const Book book = makeBook(Kind::Split, n);
  resetCard(book);
  ASSERT_TRUE(writeChapterList(book));
  EXPECT_EQ(zipModel.scans, 0u);
  EXPECT_EQ(zipModel.scannedEntries, 0u);
  EXPECT_FALSE(Storage.exists(binPath.c_str()));
  EXPECT_FALSE(Storage.exists((cachePath + "/spine.bin.tmp").c_str())) << "book.part replaces the spine pass file";

  BookMetadataCache complete(cachePath);
  EXPECT_FALSE(complete.load()) << "a caller that needs sizes never gets a partial index";

  BookMetadataCache cache(cachePath);
  ASSERT_TRUE(cache.load(/*allowPartial=*/true));
  EXPECT_TRUE(cache.isPartial());
  EXPECT_EQ(cache.getSpineCount(), n);
  EXPECT_EQ(cache.getTocCount(), 0);
  EXPECT_EQ(cache.coreMetadata.title, "Sách thử");
  EXPECT_EQ(cache.tocSource.navItem, basePath + "nav.xhtml");
  EXPECT_EQ(cache.tocSource.basePath, basePath);
  for (int i : {0, 1, 3999, n - 1}) {
    const auto spine = cache.getSpineEntry(i);
    EXPECT_EQ(spine.href, basePath + "c" + pad5(i + 1) + ".xhtml");
    EXPECT_EQ(spine.tocIndex, -1);
    EXPECT_EQ(cache.getCumulativeSize(i), 0u) << "no size may pass for the book's progress";
  }
}

// The background steps end in the very book.bin the one-pass build writes.
TEST(BackgroundIndex, StepsBuildTheSameBookBinAsOnePass) {
  for (int n : {400, 2000, 5000}) {
    const Book book = makeBook(Kind::Split, n);
    const auto expected = onePassBookBin(book);
    ASSERT_FALSE(expected.empty());
    ASSERT_TRUE(indexInBackground(book)) << n;
    EXPECT_EQ(fileBytes(binPath), expected) << n;
    EXPECT_FALSE(Storage.exists((cachePath + "/book.bin.tmp").c_str()));
    BookMetadataCache cache(cachePath);
    ASSERT_TRUE(cache.load(/*allowPartial=*/true));
    EXPECT_FALSE(cache.isPartial());
    cache.removePartFiles();
    EXPECT_FALSE(Storage.exists(partPath.c_str()));
    expectSplitCache(n);
  }
}

// A TOC with an entry for a file outside the spine, two entries for one chapter and chapters with
// none: the chunked match gives the same chapters the in-memory index gave.
TEST(BackgroundIndex, TocMatchingAgreesWithOnePassOnIrregularTocs) {
  Book book = makeBook(Kind::Split, 1200);
  {
    heapcap::Untracked guard;
    const std::string extra = "<li><a href=\"missing.xhtml\">Lạc</a></li><li><a href=\"c00007.xhtml\">Lặp</a></li>";
    const size_t at = book.nav.find("<li><a href=\"c00010.xhtml\"");
    book.nav.insert(at, extra);
    const std::string gap = "<li><a href=\"c00500.xhtml\">Chương 500: Mưa nắng</a></li>";
    book.nav.erase(book.nav.find(gap), gap.size());
  }
  const auto expected = onePassBookBin(book);
  ASSERT_TRUE(indexInBackground(book));
  EXPECT_EQ(fileBytes(binPath), expected);
}

// Every stop point of the book.bin step leaves no book.bin and no temporary file behind, and the
// next run from the same card still ends in the one-pass bytes.
TEST(BackgroundIndex, StoppedBookStepLeavesNothingAndResumes) {
  const Book book = makeBook(Kind::Split, 2000);
  const auto expected = onePassBookBin(book);
  resetCard(book);
  ASSERT_TRUE(writeChapterList(book) && tocStep(book));
  int stopPoints = 0;
  for (int k = 1;; ++k) {
    stopAfter = k;
    stopAsked = 0;
    const bool built = bookStep(stopCounter);
    if (built) break;
    ++stopPoints;
    EXPECT_FALSE(Storage.exists(binPath.c_str())) << "stop " << k;
    EXPECT_FALSE(Storage.exists((cachePath + "/book.bin.tmp").c_str())) << "stop " << k;
    ASSERT_LT(k, 1000);
  }
  EXPECT_GT(stopPoints, 10) << "the step must be stoppable all along";
  EXPECT_EQ(fileBytes(binPath), expected);
}

// A TOC pass cut before its end (stop, power cut, sleep) is never taken for a finished one.
TEST(BackgroundIndex, CutTocPassIsRedoneFromTheStart) {
  const Book book = makeBook(Kind::Split, 2000);
  const auto expected = onePassBookBin(book);
  resetCard(book);
  ASSERT_TRUE(writeChapterList(book));
  ASSERT_TRUE(tocStep(book, /*finish=*/false));
  EXPECT_FALSE(BookMetadataCache::deferredTocReady(cachePath));
  EXPECT_FALSE(bookStep()) << "book.bin cannot be built on a cut TOC pass";
  EXPECT_FALSE(Storage.exists(binPath.c_str()));
  // The count lands after toc.bin: toc.bin alone is not a finished pass either.
  ASSERT_TRUE(tocStep(book));
  {
    heapcap::Untracked guard;
    Storage.files.erase(cachePath + "/toc.count");
  }
  EXPECT_FALSE(BookMetadataCache::deferredTocReady(cachePath));
  ASSERT_TRUE(tocStep(book) && bookStep());
  EXPECT_EQ(fileBytes(binPath), expected);
}

// The step that swaps the index in stops like the others, and fails without touching the card.
TEST(BackgroundIndex, StoppedLoadFailsAndLoadsLater) {
  const Book book = makeBook(Kind::Split, 5000);
  ASSERT_TRUE(indexInBackground(book));
  stopAfter = 3;
  stopAsked = 0;
  BookMetadataCache stopped(cachePath);
  EXPECT_FALSE(stopped.load(false, stopCounter));
  EXPECT_FALSE(stopped.isLoaded());
  BookMetadataCache later(cachePath);
  EXPECT_TRUE(later.load(false, neverStop));
  EXPECT_FALSE(later.isPartial());
}

// A cache written by the previous release (book.bin, version 10) still opens as a whole index.
TEST(BackgroundIndex, OnePassCacheStillLoadsWhole) {
  const Book book = makeBook(Kind::Split, 5000);
  resetCard(book);
  ASSERT_TRUE(indexBook(book, SIZE_MAX).ok);
  BookMetadataCache cache(cachePath);
  ASSERT_TRUE(cache.load(/*allowPartial=*/true));
  EXPECT_FALSE(cache.isPartial());
  EXPECT_EQ(cache.getTocCount(), 5000);
}

// The reader runs these steps with the radio held off but beside a page: the TOC step keeps no
// index of the chapters in memory, and the book.bin step fits beside the reader.
TEST(BackgroundIndex, StepsFitBesideTheReader) {
  constexpr size_t READER_IDLE_HEAP = 60 * 1024;
  const Book book = makeBook(Kind::Split, 5000);
  resetCard(book);
  ASSERT_TRUE(writeChapterList(book));
  heapcap::reset(SIZE_MAX);
  ASSERT_TRUE(tocStep(book));
  const size_t tocPeak = heapcap::peak;
  heapcap::reset(READER_IDLE_HEAP);
  const bool built = bookStep();
  const size_t bookPeak = heapcap::peak;
  const unsigned aborts = heapcap::aborts;
  heapcap::stop();
  printf("HUGE_INDEX background n=5000 peak_toc=%zu peak_book=%zu cap=%zu\n", tocPeak, bookPeak, READER_IDLE_HEAP);
  EXPECT_EQ(aborts, 0u);
  EXPECT_TRUE(built);
  // The one-pass TOC pass holds 8 bytes per chapter (40 KB here) on top of the parser.
  EXPECT_LT(tocPeak, 5000u * 8);
}
