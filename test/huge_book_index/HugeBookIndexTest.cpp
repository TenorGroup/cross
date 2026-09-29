// Every operator new made while `heap::cap` is set is charged against it. The firmware has no
// exceptions, so a throwing allocation past the cap is where the device would abort().
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <new>
#include <string>

#include "Epub/BookMetadataCache.h"
#include "ZipFile.h"

namespace heap {
size_t cap = SIZE_MAX;  // SIZE_MAX: not counting
size_t live = 0;
unsigned aborts = 0;

struct alignas(std::max_align_t) Header {
  size_t size;
  bool counted;
};

void* allocate(const size_t size, const bool nothrow) {
  const bool counted = cap != SIZE_MAX;
  if (counted && live + size > cap) {
    if (nothrow) return nullptr;
    aborts++;
  }
  auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
  if (!header) std::abort();
  header->size = size;
  header->counted = counted;
  if (counted) live += size;
  return header + 1;
}

void release(void* p) {
  if (!p) return;
  auto* header = static_cast<Header*>(p) - 1;
  if (header->counted) live -= header->size;
  std::free(header);
}
}  // namespace heap

void* operator new(const size_t size) { return heap::allocate(size, false); }
void* operator new[](const size_t size) { return heap::allocate(size, false); }
void* operator new(const size_t size, const std::nothrow_t&) noexcept { return heap::allocate(size, true); }
void* operator new[](const size_t size, const std::nothrow_t&) noexcept { return heap::allocate(size, true); }
void operator delete(void* p) noexcept { heap::release(p); }
void operator delete[](void* p) noexcept { heap::release(p); }
void operator delete(void* p, size_t) noexcept { heap::release(p); }
void operator delete[](void* p, size_t) noexcept { heap::release(p); }

namespace {
// An X3 opening a 5,000-chapter book had 116,012 B free; a few KB go to malloc users.
constexpr size_t OPEN_HEAP = 110 * 1024;

std::string chapterHref(const int i) { return "OEBPS/c" + std::to_string(i) + ".xhtml"; }
uint32_t chapterBytes(const int i) { return 8000 + static_cast<uint32_t>(i * 37 % 9000); }
}  // namespace

// 5,000 chapters abort on develop. Sizes must land on the right chapter, with one zip scan per
// 2,048 chapters.
TEST(HugeBookIndex, IndexesUnderTheHeapOfAnOpenBook) {
  const std::string dir = testing::TempDir() + "huge_book_index";
  for (const int spineCount : {2000, 5000}) {
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    zipEntries.clear();
    zipScans = 0;
    for (int i = 0; i < spineCount; i++) zipEntries.emplace_back(chapterHref(i), chapterBytes(i));
    {
      BookMetadataCache cache(dir);
      heap::live = 0;
      heap::aborts = 0;
      heap::cap = OPEN_HEAP;
      EXPECT_TRUE(cache.beginWrite() && cache.beginContentOpfPass());
      for (int i = 0; i < spineCount; i++) cache.createSpineEntry(chapterHref(i));
      EXPECT_TRUE(cache.endContentOpfPass() && cache.beginTocPass());
      for (int i = 0; i < spineCount; i++) cache.createTocEntry("Chapter " + std::to_string(i), chapterHref(i), "", 1);
      EXPECT_TRUE(cache.endTocPass() && cache.endWrite());
      EXPECT_TRUE(cache.buildBookBin("book.epub", {}));
      heap::cap = SIZE_MAX;
    }
    EXPECT_EQ(heap::aborts, 0u) << spineCount << " chapters: the device would abort";
    EXPECT_EQ(zipScans, (spineCount + 2047u) / 2048) << spineCount << " chapters";

    BookMetadataCache cache(dir);
    ASSERT_TRUE(cache.load());
    uint32_t total = 0;
    for (int i = 0; i < spineCount; i++) {
      total += chapterBytes(i);
      ASSERT_EQ(cache.getCumulativeSize(i), total) << spineCount << " chapters, spine " << i;
    }
  }
}
