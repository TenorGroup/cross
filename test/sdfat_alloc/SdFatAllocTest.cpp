// SdFat's FAT cluster search over a sparse in-memory card, counting the sectors it reads.
// On the X3 a new file right after a book cache was deleted took 3,3 s, and a new cache
// folder 4,3 s: freeing a cluster pulled the search start down to it, and the allocation
// after the freed ones walked every used cluster above them, one FAT sector at a time.
#include <FatLib/FatLib.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <unordered_map>

namespace {
class SparseCard final : public FsBlockDeviceInterface {
 public:
  explicit SparseCard(Sector_t sectors) : sectors(sectors) {}
  size_t reads = 0;
  bool isBusy() override { return false; }
  bool readSector(Sector_t sector, uint8_t* dst) override {
    ++reads;
    const auto it = data.find(sector);
    if (it == data.end()) {
      memset(dst, 0, 512);
    } else {
      memcpy(dst, it->second.data(), 512);
    }
    return sector < sectors;
  }
  bool readSectors(Sector_t sector, uint8_t* dst, size_t count) override {
    for (size_t i = 0; i < count; ++i) readSector(sector + i, dst + 512 * i);
    return true;
  }
  Sector_t sectorCount() override { return sectors; }
  bool syncDevice() override { return true; }
  bool writeSector(Sector_t sector, const uint8_t* src) override {
    memcpy(data[sector].data(), src, 512);
    return sector < sectors;
  }
  bool writeSectors(Sector_t sector, const uint8_t* src, size_t count) override {
    for (size_t i = 0; i < count; ++i) writeSector(sector + i, src + 512 * i);
    return true;
  }

 private:
  Sector_t sectors;
  std::unordered_map<Sector_t, std::array<uint8_t, 512>> data;
};

struct Card {
  SparseCard device;
  FatVolume volume;
  explicit Card(uint64_t bytes) : device(static_cast<Sector_t>(bytes / 512)) {
    uint8_t buffer[512];
    FatFormatter formatter;
    EXPECT_TRUE(formatter.format(&device, buffer));
    EXPECT_TRUE(volume.begin(&device));
  }
  bool write(const char* path, const void* bytes, size_t size) {
    FatFile file;
    if (!file.open(&volume, path, O_RDWR | O_CREAT | O_TRUNC)) return false;
    const bool ok = file.write(bytes, size) == static_cast<int>(size);
    return file.close() && ok;
  }
  bool fill(const char* path, uint64_t bytes) {
    FatFile file;
    return file.open(&volume, path, O_RDWR | O_CREAT) && file.preAllocate(bytes) && file.close();
  }
  // Card sectors read to create one small file.
  size_t readsToCreate(const char* path) {
    const size_t before = device.reads;
    EXPECT_TRUE(write(path, "x", 1));
    return device.reads - before;
  }
};
}  // namespace

// A 16 GB FAT32 card half full, as on the X3: an old file near the start, gigabytes above it.
TEST(SdFatAlloc, NewFilesAfterADeleteDoNotWalkTheUsedClusters) {
  Card card(16ull << 30);
  ASSERT_EQ(card.volume.fatType(), 32);
  ASSERT_TRUE(card.write("/old.json", "{}", 2));
  // FAT32 files stop short of 4 GB: two of them.
  ASSERT_TRUE(card.fill("/books1.bin", (4ull << 30) - (1u << 20)));
  ASSERT_TRUE(card.fill("/books2.bin", (4ull << 30) - (1u << 20)));
  ASSERT_TRUE(card.volume.remove("/old.json"));
  const size_t first = card.readsToCreate("/a.bin");
  const size_t second = card.readsToCreate("/b.bin");
  const size_t third = card.readsToCreate("/c.bin");
  printf("SDFAT_ALLOC reads first=%zu second=%zu third=%zu\n", first, second, third);
  // Unpatched, one of these walks 8 GB of used clusters: some 2.000 FAT sectors.
  EXPECT_LT(first + second + third, 60u);
}

// The X3 mounts the card on every wake. SdFat began each mount's search at the first cluster, so
// the first new file after a wake walked every used cluster below the free space.
TEST(SdFatAlloc, AMountStartsWhereTheLastSessionStopped) {
  Card card(16ull << 30);
  ASSERT_TRUE(card.fill("/books1.bin", (4ull << 30) - (1u << 20)));
  ASSERT_TRUE(card.fill("/books2.bin", (4ull << 30) - (1u << 20)));
  ASSERT_TRUE(card.write("/last.bin", "x", 1));
  FatVolume woken;
  ASSERT_TRUE(woken.begin(&card.device));
  const size_t before = card.device.reads;
  FatFile file;
  ASSERT_TRUE(file.open(&woken, "/new.bin", O_RDWR | O_CREAT));
  ASSERT_EQ(file.write("y", 1), 1);
  ASSERT_TRUE(file.close());
  const size_t reads = card.device.reads - before;
  printf("SDFAT_ALLOC first_after_mount reads=%zu\n", reads);
  // Unpatched: some 2.000 FAT sectors below the free space.
  EXPECT_LT(reads, 30u);
}

// The hint is only a hint: one pointing into used clusters, or past the card, still allocates.
TEST(SdFatAlloc, AWrongHintStillAllocates) {
  Card card(4ull << 30);
  ASSERT_EQ(card.volume.fatType(), 32);
  ASSERT_TRUE(card.write("/a.bin", "a", 1));
  ASSERT_TRUE(card.write("/b.bin", "b", 1));
  // The partition's first sector from the MBR; FSInfo is the sector after it, next-free at 492.
  uint8_t sector[512];
  card.device.readSector(0, sector);
  const uint32_t start = sector[454] | sector[455] << 8 | sector[456] << 16 | uint32_t{sector[457]} << 24;
  for (const uint32_t hint : {3u, 0x7FFFFFFFu}) {
    card.device.readSector(start + 1, sector);
    ASSERT_EQ(sector[0], 0x52);  // "RRaA", the FSInfo lead signature
    memcpy(sector + 492, &hint, 4);
    card.device.writeSector(start + 1, sector);
    FatVolume woken;
    ASSERT_TRUE(woken.begin(&card.device));
    FatFile file;
    char name[16];
    snprintf(name, sizeof(name), "/h%u.bin", hint);
    ASSERT_TRUE(file.open(&woken, name, O_RDWR | O_CREAT));
    EXPECT_EQ(file.write("y", 1), 1) << "hint " << hint;
    EXPECT_TRUE(file.close());
    FatFile a;
    char back = 0;
    ASSERT_TRUE(a.open(&woken, "/a.bin", O_RDONLY));
    EXPECT_EQ(a.read(&back, 1), 1);
    EXPECT_EQ(back, 'a') << "a new file must not take a used cluster";
  }
}

// With the search start moving forward only, a cluster freed below it must still be found once
// every cluster above it is taken.
TEST(SdFatAlloc, ClustersFreedBelowTheStartAreFoundWhenTheCardIsFull) {
  Card card(64ull << 20);
  const uint32_t clusterBytes = card.volume.bytesPerCluster();
  ASSERT_TRUE(card.write("/low.bin", "l", 1));
  ASSERT_TRUE(card.write("/mid.bin", "m", 1));
  // Everything but two clusters in one run, then those two: the search start sits at the end.
  const int32_t free = card.volume.freeClusterCount();
  ASSERT_GT(free, 10);
  ASSERT_TRUE(card.fill("/big.bin", static_cast<uint64_t>(free - 2) * clusterBytes));
  ASSERT_TRUE(card.write("/f0.bin", "f", 1));
  ASSERT_TRUE(card.write("/f1.bin", "f", 1));
  ASSERT_FALSE(card.write("/none.bin", "n", 1)) << "the card is full";
  card.volume.remove("/none.bin");
  ASSERT_TRUE(card.volume.remove("/low.bin"));
  ASSERT_TRUE(card.write("/again.bin", "a", 1)) << "the freed cluster must be reused";
  ASSERT_FALSE(card.write("/none.bin", "n", 1)) << "and then the card is full again";
  card.volume.remove("/none.bin");
  // A file grows into freed clusters on both sides of the search start, and reads back whole.
  ASSERT_TRUE(card.volume.remove("/mid.bin"));
  ASSERT_TRUE(card.volume.remove("/f1.bin"));
  std::vector<uint8_t> two(clusterBytes + 1, 7);
  ASSERT_TRUE(card.write("/two.bin", two.data(), two.size()));
  FatFile file;
  ASSERT_TRUE(file.open(&card.volume, "/two.bin", O_RDONLY));
  std::vector<uint8_t> back(two.size());
  EXPECT_EQ(file.read(back.data(), back.size()), static_cast<int>(back.size()));
  EXPECT_EQ(back, two);
  EXPECT_EQ(card.volume.freeClusterCount(), 0);
  // A contiguous run freed below the search start is found the same way.
  ASSERT_TRUE(card.volume.remove("/big.bin"));
  EXPECT_TRUE(card.fill("/run.bin", 4ull * clusterBytes));
}
