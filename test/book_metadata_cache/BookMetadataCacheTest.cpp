#include <BookMetadataCache.h>
#include <Serialization.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Catch corrupt lengths before the host actually attempts a device-sized OOM.
static size_t largestRequest = 0;
void* operator new(size_t size) {
  largestRequest = std::max(largestRequest, size);
  if (size > 1024 * 1024) throw std::bad_alloc();
  if (void* p = std::malloc(size)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
#define REQUIRE(x)                          \
  do {                                      \
    if (!(x)) throw std::runtime_error(#x); \
  } while (0)

static void put32(std::vector<uint8_t>& b, size_t p, uint32_t value) { memcpy(b.data() + p, &value, 4); }
static std::shared_ptr<TestFile> fixture(int count = 2044) {
  HalFile f;
  f.data = std::make_shared<TestFile>();
  serialization::writePod(f, uint8_t{10});
  serialization::writePod(f, uint32_t{0});
  serialization::writePod(f, uint16_t(count));
  serialization::writePod(f, uint16_t(count));
  for (int i = 0; i < 5; ++i) serialization::writeString(f, std::string("metadata"));
  const uint32_t lut = f.position();
  for (int i = 0; i < count * 2; ++i) serialization::writePod(f, uint32_t{0});
  for (int i = 0; i < count; ++i) {
    put32(f.data->bytes, lut + i * 4, f.position());
    serialization::writeString(f, "OEBPS/chapter" + std::to_string(i) + ".xhtml");
    serialization::writePod(f, uint32_t((i + 1) * 25704));
    serialization::writePod(f, int16_t(i));
  }
  for (int i = 0; i < count; ++i) {
    put32(f.data->bytes, lut + (count + i) * 4, f.position());
    serialization::writeString(f, "Chapter " + std::to_string(i));
    serialization::writeString(f, "OEBPS/chapter" + std::to_string(i) + ".xhtml");
    serialization::writeString(f, "anchor");
    serialization::writePod(f, uint8_t{0});
    serialization::writePod(f, int16_t(i));
  }
  put32(f.data->bytes, 1, lut);
  return f.data;
}
static void healthyLargeBook() {
  Storage.files["/test/book.bin"] = fixture();
  BookMetadataCache cache("/test");
  REQUIRE(cache.load());
  REQUIRE(cache.getSpineCount() == 2044);
  REQUIRE(cache.getTocEntry(2043).title == "Chapter 2043");
  REQUIRE(cache.getSpineEntry(2043).tocIndex == 2043);
  REQUIRE(cache.getCumulativeSize(2043) == 2044u * 25704);
}
static void truncatedRead() {
  auto f = fixture();
  f->readable = 98304;
  f->seekFailureAt = 98304;
  Storage.files["/test/book.bin"] = f;
  BookMetadataCache cache("/test");
  REQUIRE(!cache.load());
  REQUIRE(!cache.isLoaded());
}
static void runtimeSeekFailure() {
  auto f = fixture(3);
  Storage.files["/test/book.bin"] = f;
  BookMetadataCache cache("/test");
  REQUIRE(cache.load());
  f->seekFailureAt = 0;
  REQUIRE(cache.getTocEntry(2).spineIndex == -1);
  REQUIRE(cache.getSpineEntry(2).tocIndex == -1);
}
static void oversizedTitle() {
  auto f = fixture(3);
  put32(f->bytes, 9, 0x496c6d74);
  Storage.files["/test/book.bin"] = f;
  BookMetadataCache cache("/test");
  REQUIRE(!cache.load());
}
static void badLut() {
  auto f = fixture(3);
  put32(f->bytes, 1, 0xfffffff0);
  Storage.files["/test/book.bin"] = f;
  BookMetadataCache cache("/test");
  REQUIRE(!cache.load());
}
static void shortHeader() {
  auto f = fixture(3);
  f->bytes.resize(4);
  Storage.files["/test/book.bin"] = f;
  BookMetadataCache cache("/test");
  REQUIRE(!cache.load());
}
static void brokenEntryPointer() {
  auto f = fixture(3);
  uint32_t lut;
  memcpy(&lut, f->bytes.data() + 1, 4);
  put32(f->bytes, lut + 5 * 4, 0);
  Storage.files["/test/book.bin"] = f;
  BookMetadataCache cache("/test");
  REQUIRE(!cache.load());
}
static void runtimeShortRead() {
  auto f = fixture(3);
  Storage.files["/test/book.bin"] = f;
  BookMetadataCache cache("/test");
  REQUIRE(cache.load());
  f->readable = 0;
  REQUIRE(cache.getTocEntry(2).spineIndex == -1);
}
static void reloadFailureClearsLoaded() {
  auto f = fixture(3);
  Storage.files["/test/book.bin"] = f;
  BookMetadataCache cache("/test");
  REQUIRE(cache.load());
  f->bytes[0] = 99;
  REQUIRE(!cache.load());
  REQUIRE(!cache.isLoaded());
  REQUIRE(cache.getCumulativeSize(1) == 0);
}
static void sequentialScan() {
  auto f = fixture();
  Storage.files["/test/book.bin"] = f;
  BookMetadataCache cache("/test");
  REQUIRE(cache.load());
  f->reads = f->seeks = 0;
  for (int i = 0; i < 2044; ++i) REQUIRE(cache.getTocEntry(i).spineIndex == i);
  const auto randomReads = f->reads;
  f->reads = f->seeks = 0;
  auto cursor = cache.openTocCursor();
  REQUIRE(cursor);
  BookMetadataCache::TocEntry entry;
  for (int i = 0; i < 2044; ++i) {
    REQUIRE(cursor->next(entry));
    REQUIRE(entry.title == "Chapter " + std::to_string(i));
    REQUIRE(entry.spineIndex == i);
  }
  REQUIRE(!cursor->next(entry));
  REQUIRE(!cursor->failed());
  REQUIRE(f->reads * 50 < randomReads);
  REQUIRE(f->seeks == 2);
  std::cout << "TOC reads: random=" << randomReads << " sequential=" << f->reads << '\n';
  cursor = cache.openTocCursor(1300);
  REQUIRE(cache.getTocEntry(10).spineIndex == 10);
  REQUIRE(cursor->next(entry));
  REQUIRE(entry.spineIndex == 1300);
  REQUIRE(!cache.openTocCursor(-1));
  REQUIRE(!cache.openTocCursor(2044));
}
static void cursorRuntimeFailure() {
  auto f = fixture();
  Storage.files["/test/book.bin"] = f;
  BookMetadataCache cache("/test");
  REQUIRE(cache.load());
  auto cursor = cache.openTocCursor();
  REQUIRE(cursor);
  BookMetadataCache::TocEntry entry;
  for (int i = 0; i < 1000; ++i) REQUIRE(cursor->next(entry));
  f->readable = 0;
  int buffered = 0;
  while (cursor->next(entry)) ++buffered;
  REQUIRE(buffered < 100);
  REQUIRE(cursor->failed());
  REQUIRE(!cursor->next(entry));
  f->readable = SIZE_MAX;
  f->seekFailureAt = 0;
  REQUIRE(!cache.openTocCursor());
}
int main(int argc, char** argv) {
  if (argc == 2) {
    std::ifstream in(argv[1], std::ios::binary);
    auto f = std::make_shared<TestFile>();
    f->bytes.assign(std::istreambuf_iterator<char>(in), {});
    f->bytes.resize(234351);
    f->readable = f->seekFailureAt = 98304;
    Storage.files["/test/book.bin"] = f;
    BookMetadataCache cache("/test");
    try {
      bool loaded = cache.load();
      std::cout << "loaded=" << loaded << std::endl;
      if (loaded) cache.getTocEntry(2042);
      std::cout << "largest allocation=" << largestRequest << std::endl;
      return loaded ? 1 : 0;
    } catch (const std::bad_alloc&) {
      std::cerr << "blocked allocation=" << largestRequest << std::endl;
      return 1;
    }
  }
  struct Case {
    const char* name;
    void (*run)();
  } cases[] = {{"sequential scan and random interleave", sequentialScan},
               {"cursor runtime failure", cursorRuntimeFailure},
               {"healthy 2044 chapters", healthyLargeBook},
               {"96KiB read failure", truncatedRead},
               {"runtime seek failure", runtimeSeekFailure},
               {"oversized title", oversizedTitle},
               {"overflow LUT", badLut},
               {"short header", shortHeader},
               {"invalid entry pointer", brokenEntryPointer},
               {"runtime short read", runtimeShortRead},
               {"reload invalidates state", reloadFailureClearsLoaded}};
  int failed = 0;
  for (const auto& c : cases) {
    try {
      c.run();
      std::cout << "PASS " << c.name << '\n';
    } catch (const std::exception& e) {
      ++failed;
      std::cerr << "FAIL " << c.name << ": " << e.what() << '\n';
    }
  }
  return failed ? 1 : 0;
}
