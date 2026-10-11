#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <new>
#include <set>
#ifdef __APPLE__
#include <dlfcn.h>
#endif

#include "shells/ugly/UglyIconInk.h"
#include "shells/Shell.h"
#include "components/icons/tenorRowIcons.h"

namespace {
bool watchAllocations = false;
unsigned allocations = 0;

uint64_t hash(const GfxRenderer& renderer) {
  uint64_t value = 1469598103934665603ull;
  for (const auto pixel : renderer.pixels) value = (value ^ pixel) * 1099511628211ull;
  return value;
}

uint64_t contourHash(const GfxRenderer& renderer) {
  int minX = 64, minY = 64;
  for (int row = 0; row < 64; ++row) for (int column = 0; column < 64; ++column)
    if (renderer.pixels[row * 64 + column]) { minX = std::min(minX, column); minY = std::min(minY, row); }
  uint64_t value = 1469598103934665603ull;
  for (int row = 0; row < 64; ++row) for (int column = 0; column < 64; ++column) {
    const auto pixel = row + minY < 64 && column + minX < 64 ? renderer.pixels[(row + minY) * 64 + column + minX] : 0;
    value = (value ^ pixel) * 1099511628211ull;
  }
  return value;
}

int sharpTurns(ugly::icons::Id id, int size, uint32_t seed) {
  ugly::icons::Point previousEnd{}, direction{};
  bool haveDirection = false;
  int count = 0;
  ugly::icons::walk(id, {0, 0, size, size}, seed, [&](auto first, auto second) {
    const ugly::icons::Point next{second.x - first.x, second.y - first.y};
    const double length = std::hypot(next.x, next.y);
    if (length < 64) return;
    if (haveDirection && previousEnd.x == first.x && previousEnd.y == first.y) {
      const double product = static_cast<double>(direction.x) * next.x + static_cast<double>(direction.y) * next.y;
      const double cosine = product / (std::hypot(direction.x, direction.y) * length);
      if (cosine < 0.5) ++count;
    }
    direction = next;
    previousEnd = second;
    haveDirection = true;
  });
  return count;
}
}

void* operator new(std::size_t count) {
  if (watchAllocations) ++allocations;
  if (void* memory = std::malloc(count)) return memory;
  throw std::bad_alloc();
}
void* operator new[](std::size_t count) { return ::operator new(count); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }

TEST(UglyIcons, RasterIsDeterministicVariedBoundedAndHeapFree) {
  using ugly::icons::Id;
#ifdef __APPLE__
  const auto watchHeap = reinterpret_cast<void (*)(bool)>(dlsym(RTLD_DEFAULT, "uglyWatchHeap"));
  const auto heapCount = reinterpret_cast<unsigned (*)()>(dlsym(RTLD_DEFAULT, "uglyHeapCount"));
  if (std::getenv("UGLY_REQUIRE_LIBC_HEAP_PROBE")) {
    ASSERT_NE(watchHeap, nullptr);
    ASSERT_NE(heapCount, nullptr);
    watchHeap(true);
    auto* sample = static_cast<volatile unsigned char*>(std::malloc(8));
    ASSERT_NE(sample, nullptr);
    *sample = 7;
    watchHeap(false);
    EXPECT_GT(heapCount(), 0u);
    std::free(const_cast<unsigned char*>(sample));
  }
#endif
  for (unsigned index = 0; index < static_cast<unsigned>(Id::Count); ++index) {
    const auto id = static_cast<Id>(index);
    for (int size : {18, 24, 32, 40}) {
      std::set<uint64_t> variants;
      std::set<uint64_t> contours;
      for (uint32_t seed = 0; seed < 20; ++seed) {
        GfxRenderer first, replay;
        allocations = 0;
#ifdef __APPLE__
        if (watchHeap) watchHeap(true);
#endif
        watchAllocations = true;
        ugly::icon(first, id, {8, 8, size, size}, seed);
        ugly::icon(replay, id, {8, 8, size, size}, seed);
        watchAllocations = false;
#ifdef __APPLE__
        if (watchHeap) watchHeap(false);
        if (heapCount) EXPECT_EQ(heapCount(), 0u) << index << ':' << seed;
#endif
        EXPECT_EQ(allocations, 0u) << index;
        EXPECT_EQ(first.outside, 0u);
        EXPECT_EQ(hash(first), hash(replay));
        unsigned ink = 0;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
          if (!first.pixels[y * 64 + x]) continue;
          ++ink;
          EXPECT_GE(x, 10);
          EXPECT_GE(y, 10);
          EXPECT_LT(x, 8 + size - 1);
          EXPECT_LT(y, 8 + size - 1);
        }
        EXPECT_GT(ink, 5u);
        variants.insert(hash(first));
        contours.insert(contourHash(first));
        EXPECT_LE(sharpTurns(id, size, seed), sharpTurns(id, size, 0) + 3) << index << ':' << seed;
      }
      std::cout << "icon=" << index << " size=" << size << " variants=" << variants.size()
                << " contours=" << contours.size() << " turns=" << sharpTurns(id, size, 0) << '\n';
      EXPECT_GE(variants.size(), 18u) << index << ':' << size;
      EXPECT_GE(contours.size(), 18u) << index << ':' << size;
    }
  }
}

TEST(UglyIcons, NavigationInkIsCenteredForEverySeed) {
  using ugly::icons::Id;
  for (const auto id : {Id::Tick, Id::Back, Id::Left, Id::Right, Id::Up, Id::Down}) {
    for (uint32_t seed = 0; seed < 100; ++seed) {
      GfxRenderer renderer;
      ugly::icon(renderer, id, {8, 8, 24, 24}, seed);
      int minX = 64, minY = 64, maxX = -1, maxY = -1;
      for (int row = 0; row < 64; ++row) {
        for (int column = 0; column < 64; ++column) {
          if (!renderer.pixels[row * 64 + column]) continue;
          minX = std::min(minX, column);
          minY = std::min(minY, row);
          maxX = std::max(maxX, column);
          maxY = std::max(maxY, row);
        }
      }
      ASSERT_GE(maxX, minX);
      EXPECT_LE(std::abs(minX + maxX - 40), 1) << static_cast<int>(id) << ':' << seed;
      EXPECT_LE(std::abs(minY + maxY - 40), 1) << static_cast<int>(id) << ':' << seed;
    }
  }
}

TEST(UglyIcons, EntryAndPageSeedsSurviveIdleRepaint) {
  ugly::icons::FrameState frame;
  frame.enter(42);
  EXPECT_FALSE(frame.needsSeed(0));
  EXPECT_EQ(frame.bind(0, 5), 42u);
  EXPECT_FALSE(frame.needsSeed(0));
  EXPECT_EQ(frame.bind(0, 99), 42u);
  EXPECT_TRUE(frame.needsSeed(1));
  EXPECT_EQ(frame.bind(1, 71), 71u);
  EXPECT_EQ(frame.bind(1, 800), 71u);
  EXPECT_EQ(frame.bind(0, 123), 123u);
  frame.enter(7);
  EXPECT_EQ(frame.bind(0, 0), 7u);
}

TEST(UglyIcons, DistortionStaysNearApprovedSeedSeven) {
  using namespace ugly::icons;
  for (unsigned index = 0; index < static_cast<unsigned>(Id::Count); ++index) {
    const auto id = static_cast<Id>(index);
    const auto path = PATHS[index];
    auto energy = [&](const Warp& warp) {
      double total = 0;
      for (int nodeIndex = 0; nodeIndex < path.count; ++nodeIndex) {
        if (path.nodes[nodeIndex].x < 0) continue;
        const auto offset = warp.offset(path.nodes[nodeIndex]);
        total += double(offset.x) * offset.x + double(offset.y) * offset.y;
      }
      return total;
    };
    const uint32_t referenceSeed = 7u ^ index * 83492791u;
    const Warp reference{ugly::logic::wobble(referenceSeed, 1, 10), ugly::logic::wobble(referenceSeed, 2, 10),
                         ugly::logic::wobble(referenceSeed, 3, 16), ugly::logic::wobble(referenceSeed, 4, 12),
                         ugly::logic::wobble(referenceSeed, 7, 9), 0, 0};
    const auto referenceEnergy = energy(reference);
    ASSERT_GT(referenceEnergy, 0);
    for (uint32_t seed = 0; seed < 20; ++seed) {
      const double ratio = std::sqrt(energy(warpFor(id, seed)) / referenceEnergy);
      std::cout << "distortion icon=" << index << " seed=" << seed << " ratio=" << ratio << '\n';
      EXPECT_GE(ratio, 0.7) << index << ':' << seed;
      EXPECT_LE(ratio, 1.3) << index << ':' << seed;
    }
  }
}

TEST(UglyIcons, ActualFileAssetAndInlineShapeDispatch) {
  GfxRenderer routed, direct;
  ASSERT_TRUE(ugly::iconAsset(routed, icon_folder_32.bits, {8, 8, 32, 32}, 123));
  ugly::icon(direct, ugly::icons::Id::Folder, {8, 8, 32, 32}, 123);
  EXPECT_EQ(hash(routed), hash(direct));
  shell::uglyMode = false;
  GfxRenderer untouched;
  const auto before = hash(untouched);
  EXPECT_FALSE(ugly::iconSymbol(untouched, 0, 24, 24, 18, true));
  EXPECT_EQ(hash(untouched), before);
  shell::uglyMode = true;
  ugly::setIconFrameSeed(123);
  // The Select key is the pen's tick under tenor/ugly.
  GfxRenderer select, tick;
  EXPECT_TRUE(ugly::iconSymbol(select, 0, 24, 24, 18, true));
  ugly::icon(tick, ugly::icons::Id::Tick, {24 - 11, 24 - 11, 22, 22}, 123);
  EXPECT_NE(hash(select), before);
  EXPECT_EQ(hash(select), hash(tick));
  EXPECT_TRUE(ugly::iconSymbol(untouched, 1, 24, 24, 18, true));
  EXPECT_NE(hash(untouched), before);
}

TEST(UglyIcons, PointBudgetAndGallery) {
  const char* destination = std::getenv("UGLY_ICON_GALLERY");
  std::ofstream output;
  if (destination) output.open(destination);
  for (unsigned index = 0; index < static_cast<unsigned>(ugly::icons::Id::Count); ++index) {
    for (uint32_t seed : {0u, 1u, 7u, 19u}) {
      unsigned segments = 0;
      ugly::icons::walk(static_cast<ugly::icons::Id>(index), {0, 0, 40, 40}, seed,
                        [&](auto, auto) { ++segments; });
      EXPECT_LE(segments, 192u);
      GfxRenderer renderer;
      ugly::icon(renderer, static_cast<ugly::icons::Id>(index), {0, 0, 40, 40}, seed);
      if (output) {
        output << index << ',' << seed << ',' << segments << ',';
        for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) output << char('0' + renderer.pixels[y * 64 + x]);
        output << '\n';
      }
    }
  }
}

TEST(UglyIcons, RowInkHas24PixelBoundsAndCenteredThinStroke) {
  const auto assets = {icon_folder_32, icon_file_text_32, icon_image_32, icon_book_32,
                       icon_file_32, icon_wifi_32, icon_library_32, icon_radio_tower_32, icon_bookmark_32};
  for (const auto& asset : assets) for (uint32_t seed = 0; seed < 40; ++seed) {
    GfxRenderer renderer;
    ASSERT_TRUE(ugly::iconAsset(renderer, asset.bits, {20, 20, 24, 24}, seed, 32, 32, true));
    int x0=64, y0=64, x1=-1, y1=-1;
    for (int y=0;y<64;++y) for(int x=0;x<64;++x) if(renderer.pixels[y*64+x]) {
      x0=std::min(x0,x); y0=std::min(y0,y); x1=std::max(x1,x); y1=std::max(y1,y);
    }
    EXPECT_EQ(std::max(x1-x0+1,y1-y0+1),24) << seed;
    EXPECT_LE(std::abs(y0+y1-63),1) << seed;
    EXPECT_GE(x0,20); EXPECT_LE(x1,43); EXPECT_GE(y0,20); EXPECT_LE(y1,43);
    GfxRenderer again;
    ASSERT_TRUE(ugly::iconAsset(again,asset.bits,{20,20,24,24},seed,32,32,true));
    EXPECT_EQ(hash(renderer),hash(again));
  }
}

TEST(UglyIcons, RowCenterMatchesBothTextHeightParities) {
  for (int height = 12; height <= 34; ++height) for (uint32_t seed = 0; seed < 40; ++seed) {
    GfxRenderer renderer;
    ASSERT_TRUE(ugly::iconAsset(renderer, icon_folder_32.bits, ugly::rowInkBox(20, 20, 20 + height),
                               seed, 32, 32, true));
    int top = 64, bottom = -1;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) if (renderer.pixels[y * 64 + x]) {
      top = std::min(top, y); bottom = std::max(bottom, y);
    }
    EXPECT_LE(std::abs(top + bottom - (39 + height)), 1) << height << ',' << seed;
  }
}
