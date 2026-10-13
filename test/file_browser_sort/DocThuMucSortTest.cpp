#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <new>

#include "DocThuMuc.h"
#include "HalStorage.h"

// Counting allocator: live and peak bytes while `counting`, and nothrow requests refused on demand
// (what the heap floor door does on the device).
namespace {
bool counting = false, refuseNothrow = false;
size_t live = 0, peak = 0;
void* take(size_t n) {
  auto* p = static_cast<size_t*>(std::malloc(n + sizeof(std::max_align_t)));
  if (!p) return nullptr;
  *p = n;
  if (counting) {
    live += n;
    if (live > peak) peak = live;
  }
  return reinterpret_cast<char*>(p) + sizeof(std::max_align_t);
}
void give(void* q) {
  if (!q) return;
  auto* p = reinterpret_cast<size_t*>(static_cast<char*>(q) - sizeof(std::max_align_t));
  if (counting) live -= *p < live ? *p : live;
  std::free(p);
}
}  // namespace
void* operator new(size_t n) {
  if (void* p = take(n)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t n) { return operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return refuseNothrow ? nullptr : take(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return refuseNothrow ? nullptr : take(n); }
void operator delete(void* p) noexcept { give(p); }
void operator delete[](void* p) noexcept { give(p); }
void operator delete(void* p, size_t) noexcept { give(p); }
void operator delete[](void* p, size_t) noexcept { give(p); }

namespace {
using filebrowser::SortMode;
constexpr size_t kBuffer = 256;

std::vector<std::string> read(SortMode mode, size_t ceiling = 371, bool* refused = nullptr) {
  char buffer[kBuffer];
  std::vector<std::string> rows;
  bool tooMany = false;
  EXPECT_TRUE(docthumuc::doc("/books", false, docthumuc::Loc::Sach, buffer, kBuffer, rows, ceiling, &tooMany, mode));
  if (refused) *refused = tooMany;
  return rows;
}

void folderOfFour() {
  fakecard::folder = "/books";
  fakecard::entries = {{"b10.epub", false, 30, 5},
                       {"zeta", true, 99, 0},
                       {"a2.epub", false, 10, 500},
                       {"notes.pdf", false, 99, 900},  // not a book: skipped
                       {"b9.txt", false, 20, 50},
                       {"alpha", true, 1, 0}};
}

TEST(DocThuMucSort, NameIsTheExistingNaturalOrder) {
  folderOfFour();
  EXPECT_EQ(read(SortMode::Name), (std::vector<std::string>{"alpha/", "zeta/", "a2.epub", "b9.txt", "b10.epub"}));
}

TEST(DocThuMucSort, ModifiedReadsTheDirentDateNewestFirst) {
  folderOfFour();
  EXPECT_EQ(read(SortMode::Modified), (std::vector<std::string>{"alpha/", "zeta/", "b10.epub", "b9.txt", "a2.epub"}));
}

TEST(DocThuMucSort, SizeReadsTheDirentSizeLargestFirst) {
  folderOfFour();
  EXPECT_EQ(read(SortMode::Size), (std::vector<std::string>{"alpha/", "zeta/", "a2.epub", "b9.txt", "b10.epub"}));
  fakecard::entries[2].size = 1;
  EXPECT_EQ(read(SortMode::Size), (std::vector<std::string>{"alpha/", "zeta/", "b9.txt", "b10.epub", "a2.epub"}));
}

TEST(DocThuMucSort, NoCeilingOrNoRoomOpensInNameOrder) {
  folderOfFour();
  const std::vector<std::string> byName{"alpha/", "zeta/", "a2.epub", "b9.txt", "b10.epub"};
  bool refused = true;
  EXPECT_EQ(read(SortMode::Modified, 0, &refused), byName);
  EXPECT_FALSE(refused);
  refuseNothrow = true;
  EXPECT_EQ(read(SortMode::Size, 371, &refused), byName);
  refuseNothrow = false;
  EXPECT_FALSE(refused);
}

TEST(DocThuMucSort, FirmwarePickerKeepsNameOrder) {
  fakecard::folder = "/books";
  fakecard::entries = {{"b.bin", false, 1, 9}, {"a.bin", false, 9, 1}};
  char buffer[kBuffer];
  std::vector<std::string> rows;
  ASSERT_TRUE(docthumuc::doc("/books", false, docthumuc::Loc::Firmware, buffer, kBuffer, rows, 371, nullptr,
                             SortMode::Modified));
  EXPECT_EQ(rows, (std::vector<std::string>{"a.bin", "b.bin"}));
}

// Peak bytes of one folder read at the X3 row ceiling: the date or size per row is the only addition.
TEST(DocThuMucSort, PeakHeapAtTheX3Ceiling) {
  for (const size_t rows : {size_t{371}, size_t{2000}}) {
    fakecard::folder = "/books";
    fakecard::entries.clear();
    for (size_t i = 0; i < rows; ++i) {
      char name[64];
      std::snprintf(name, sizeof(name), "Tap sach co ten dai so %04zu.epub", (i * 7919) % rows);
      fakecard::entries.push_back({name, false, static_cast<uint32_t>(i), i * 3});
    }
    size_t peaks[3];
    for (const auto mode : {SortMode::Name, SortMode::Modified, SortMode::Size}) {
      live = peak = 0;
      counting = true;
      { const auto list = read(mode, rows); }
      counting = false;
      peaks[static_cast<int>(mode)] = peak;
    }
    std::printf("[ HEAP ] rows=%zu peak name=%zu modified=%zu size=%zu extra=%zu\n", rows, peaks[0], peaks[1],
                peaks[2], peaks[1] - peaks[0]);
    EXPECT_LE(peaks[1], peaks[0] + rows * sizeof(uint64_t));
    EXPECT_LE(peaks[2], peaks[0] + rows * sizeof(uint64_t));
  }
}
}  // namespace
