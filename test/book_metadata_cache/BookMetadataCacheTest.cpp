#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include "Epub/BookMetadataCache.h"
#include "HalStorage.h"
#include "ZipFile.h"

// Counting allocator: heap bytes allocated while `counting` is set and not yet freed.
namespace heap {
bool counting = false;
size_t live = 0;

struct alignas(std::max_align_t) Header {
  size_t size;
  bool counted;
};

void* allocate(const size_t size) {
  auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
  if (!header) return nullptr;
  header->size = size;
  header->counted = counting;
  if (counting) live += size;
  return header + 1;
}

void release(void* p) {
  if (!p) return;
  auto* header = static_cast<Header*>(p) - 1;
  if (header->counted) live -= header->size;
  std::free(header);
}
}  // namespace heap

void* operator new(const size_t size) {
  void* p = heap::allocate(size);
  if (!p) throw std::bad_alloc();
  return p;
}
void* operator new[](const size_t size) { return operator new(size); }
void* operator new(const size_t size, const std::nothrow_t&) noexcept { return heap::allocate(size); }
void* operator new[](const size_t size, const std::nothrow_t&) noexcept { return heap::allocate(size); }
void operator delete(void* p) noexcept { heap::release(p); }
void operator delete[](void* p) noexcept { heap::release(p); }
void operator delete(void* p, size_t) noexcept { heap::release(p); }
void operator delete[](void* p, size_t) noexcept { heap::release(p); }

namespace {

constexpr char cachePath[] = "/cache";

std::string chapterHref(const int i) { return "OEBPS/Text/chapter" + std::to_string(i) + ".xhtml"; }
uint32_t chapterBytes(const int i) { return 1500 + static_cast<uint32_t>(i) * 7919 % 30000; }

// Builds book.bin for a book of `spineCount` chapters, one TOC entry each, through the cache's own writer.
void buildBook(const int spineCount) {
  Storage.clear();
  zipEntrySizes.clear();
  for (int i = 0; i < spineCount; i++) zipEntrySizes[chapterHref(i)] = chapterBytes(i);

  BookMetadataCache cache(cachePath);
  ASSERT_TRUE(cache.beginWrite());
  ASSERT_TRUE(cache.beginContentOpfPass());
  for (int i = 0; i < spineCount; i++) cache.createSpineEntry(chapterHref(i));
  ASSERT_TRUE(cache.endContentOpfPass());
  ASSERT_TRUE(cache.beginTocPass());
  for (int i = 0; i < spineCount; i++) cache.createTocEntry("Chapter " + std::to_string(i), chapterHref(i), "", 1);
  ASSERT_TRUE(cache.endTocPass());
  ASSERT_TRUE(cache.endWrite());
  BookMetadataCache::BookMetadata metadata;
  metadata.title = "Synthetic book";
  metadata.author = "Test";
  ASSERT_TRUE(cache.buildBookBin("book.epub", metadata));
  ASSERT_TRUE(cache.cleanupTmpFiles());
}

struct LoadedHeap {
  size_t resident = 0;
  size_t afterLookups = 0;
};

// Heap a loaded cache holds after load(), and after asking every spine item's cumulative size.
LoadedHeap loadAndReadSizes(const int spineCount) {
  buildBook(spineCount);
  LoadedHeap result;
  BookMetadataCache cache(cachePath);
  heap::live = 0;
  heap::counting = true;
  EXPECT_TRUE(cache.load());
  result.resident = heap::live;
  uint32_t total = 0;
  for (int i = 0; i < spineCount; i++) {
    total += chapterBytes(i);
    EXPECT_EQ(cache.getCumulativeSize(i), total) << "spine " << i;
  }
  result.afterLookups = heap::live;
  heap::counting = false;
  return result;
}

}  // namespace

// A loaded book stays in RAM for the whole reading session, next to the section being rendered and
// anything else the reader starts (radio stacks, image decoders). What it holds must not grow with
// the number of chapters: a 5,000-chapter book may hold at most 1 KB more than a 30-chapter one.
TEST(BookMetadataCacheHeap, ResidentHeapDoesNotGrowWithSpineCount) {
  const LoadedHeap small = loadAndReadSizes(30);
  printf("BOOK_CACHE_HEAP spine=30 resident=%zu after_lookups=%zu\n", small.resident, small.afterLookups);
  for (const int spineCount : {1000, 2000, 5000}) {
    const LoadedHeap large = loadAndReadSizes(spineCount);
    printf("BOOK_CACHE_HEAP spine=%d resident=%zu after_lookups=%zu\n", spineCount, large.resident, large.afterLookups);
    if (spineCount == 5000) {
      EXPECT_LE(large.resident, small.resident + 1024);
    }
    // Lookups keep nothing on the heap.
    EXPECT_EQ(large.afterLookups, large.resident);
  }
}
