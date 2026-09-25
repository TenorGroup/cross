// SdFat's FAT cluster search as the firmware builds it (scripts/patch_sdfat.py), over a sparse
// in-memory card that counts the sectors read and logs the sectors written.
// On the X3 a new file right after a book cache was deleted took 3,3 s, and a new cache folder
// 4,3 s: freeing a cluster pulled the search start down to it, and the allocation after the
// freed ones walked every used cluster above them, one FAT sector at a time. Every wake mounts
// the card again, and each mount began the search at the first cluster.
#include <FatLib/FatLib.h>
#include <SdVolumeSync.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using Sector = std::array<uint8_t, 512>;

class SparseCard final : public FsBlockDeviceInterface {
 public:
  struct Write {
    Sector_t sector;
    Sector before, after;
  };
  explicit SparseCard(Sector_t sectors) : sectors(sectors) {}
  size_t reads = 0;
  bool logWrites = false;
  std::vector<Write> writes;

  bool isBusy() override { return false; }
  bool readSector(Sector_t sector, uint8_t* dst) override {
    ++reads;
    const Sector s = peek(sector);
    memcpy(dst, s.data(), 512);
    return sector < sectors;
  }
  bool readSectors(Sector_t sector, uint8_t* dst, size_t count) override {
    for (size_t i = 0; i < count; ++i) readSector(sector + i, dst + 512 * i);
    return true;
  }
  Sector_t sectorCount() override { return sectors; }
  bool syncDevice() override { return true; }
  bool writeSector(Sector_t sector, const uint8_t* src) override {
    if (logWrites) {
      Write w{sector, peek(sector), {}};
      memcpy(w.after.data(), src, 512);
      writes.push_back(w);
    }
    memcpy(data[sector].data(), src, 512);
    return sector < sectors;
  }
  bool writeSectors(Sector_t sector, const uint8_t* src, size_t count) override {
    for (size_t i = 0; i < count; ++i) writeSector(sector + i, src + 512 * i);
    return true;
  }
  // The card's bytes, without counting a read.
  Sector peek(Sector_t sector) const {
    Sector s{};
    const auto it = data.find(sector);
    if (it != data.end()) s = it->second;
    return s;
  }
  void poke(Sector_t sector, const Sector& s) { data[sector] = s; }

 private:
  Sector_t sectors;
  std::unordered_map<Sector_t, Sector> data;
};

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t{p[3]} << 24; }
void setLe32Bytes(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

struct Card {
  SparseCard device;
  FatVolume volume;
  uint32_t partitionStart = 0;
  explicit Card(uint64_t bytes) : device(static_cast<Sector_t>(bytes / 512)) {
    uint8_t buffer[512];
    FatFormatter formatter;
    EXPECT_TRUE(formatter.format(&device, buffer));
    EXPECT_TRUE(volume.begin(&device));
    partitionStart = le32(device.peek(0).data() + 454);
  }
  // Every file closed first: SdFat's caches are clean then.
  void remount() { ASSERT_TRUE(volume.begin(&device)); }
  bool write(const char* path, const void* bytes, size_t size, bool append = false) {
    FatFile file;
    if (!file.open(&volume, path, O_RDWR | O_CREAT | (append ? O_APPEND : O_TRUNC))) return false;
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
  Sector_t fsInfoSector() const { return partitionStart + 1; }
  void setHint(uint32_t hint) {
    Sector s = device.peek(fsInfoSector());
    setLe32Bytes(s.data() + 492, hint);
    device.poke(fsInfoSector(), s);
  }

  bool isEoc(uint32_t value) const { return value >= (volume.fatType() == 32 ? 0x0FFFFFF8u : 0xFFF8u); }
  uint32_t lastCluster() const { return volume.clusterCount() + 1; }

  // fsck: every chain valid and ended, no cluster in two chains or unreachable, files as long as
  // their chains say. `preallocated` files may hold more clusters than their size needs.
  void check(const std::set<std::string>& preallocated = {}) {
    // The FAT as the card holds it, read once, sector by sector.
    std::vector<uint32_t> fat(lastCluster() + 1);
    const bool fat32 = volume.fatType() == 32;
    const uint32_t perSector = fat32 ? 128 : 256;
    for (uint32_t c = 0; c <= lastCluster(); c += perSector) {
      const Sector s = device.peek(volume.fatStartSector() + c / perSector);
      for (uint32_t i = 0; i < perSector && c + i <= lastCluster(); ++i) {
        fat[c + i] = fat32 ? le32(s.data() + 4 * i) & 0x0FFFFFFF : static_cast<uint32_t>(s[2 * i] | s[2 * i + 1] << 8);
      }
    }
    auto fatEntry = [&fat](uint32_t cluster) { return fat[cluster]; };
    std::set<uint32_t> owned;
    auto walk = [&](uint32_t first, const std::string& name) -> size_t {
      size_t length = 0;
      for (uint32_t c = first; c != 0;) {
        EXPECT_TRUE(c >= 2 && c <= lastCluster()) << name << " cluster " << c;
        if (c < 2 || c > lastCluster()) return length;
        EXPECT_TRUE(owned.insert(c).second) << name << " shares cluster " << c;
        ++length;
        const uint32_t next = fatEntry(c);
        EXPECT_NE(next, 0u) << name << " chain runs into a free cluster at " << c;
        if (isEoc(next) || next == 0 || length > volume.clusterCount()) break;
        c = next;
      }
      return length;
    };
    if (volume.fatType() == 32) walk(volume.rootDirStart(), "root");
    FatFile root, file;
    ASSERT_TRUE(root.openRoot(&volume));
    char name[64];
    while (file.openNext(&root, O_RDONLY)) {
      file.getName(name, sizeof(name));
      const size_t length = walk(file.firstCluster(), name);
      const size_t needed = (file.fileSize() + volume.bytesPerCluster() - 1) / volume.bytesPerCluster();
      if (preallocated.count(name)) {
        EXPECT_GE(length, needed) << name;
      } else {
        EXPECT_EQ(length, needed) << name;
      }
      file.close();
    }
    root.close();
    size_t used = 0;
    for (uint32_t c = 2; c <= lastCluster(); ++c) used += fat[c] != 0;
    EXPECT_EQ(used, owned.size()) << "clusters taken but in no chain";
  }
};

// Allocation only: a FAT entry may go from free to taken, or from end-of-chain to a link.
void expectAllocationOnly(const Card& card, const std::vector<SparseCard::Write>& writes) {
  const bool fat32 = card.volume.fatType() == 32;
  const uint32_t fatSectors = card.volume.sectorsPerFat() * card.volume.fatCount();
  for (const auto& w : writes) {
    if (w.sector < card.volume.fatStartSector() || w.sector >= card.volume.fatStartSector() + fatSectors) continue;
    for (size_t at = 0; at < 512; at += fat32 ? 4 : 2) {
      const uint32_t before = fat32 ? le32(w.before.data() + at) & 0x0FFFFFFF : (w.before[at] | w.before[at + 1] << 8);
      const uint32_t after = fat32 ? le32(w.after.data() + at) & 0x0FFFFFFF : (w.after[at] | w.after[at + 1] << 8);
      if (before == after) continue;
      EXPECT_TRUE(before == 0 || (card.isEoc(before) && after >= 2 && after <= card.lastCluster()))
          << "FAT sector " << w.sector << " entry " << at << ": " << before << " -> " << after;
    }
  }
}
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
  card.check({"books1.bin", "books2.bin"});
}

// The first new file after a mount starts where the last session stopped.
TEST(SdFatAlloc, AMountStartsWhereTheLastSessionStopped) {
  Card card(16ull << 30);
  ASSERT_TRUE(card.fill("/books1.bin", (4ull << 30) - (1u << 20)));
  ASSERT_TRUE(card.fill("/books2.bin", (4ull << 30) - (1u << 20)));
  ASSERT_TRUE(card.write("/last.bin", "x", 1));
  card.remount();
  const size_t reads = card.readsToCreate("/new.bin");
  printf("SDFAT_ALLOC first_after_mount reads=%zu\n", reads);
  // Unpatched: some 2.000 FAT sectors below the free space.
  EXPECT_LT(reads, 30u);
  card.check({"books1.bin", "books2.bin"});
}

// Creating and growing files only ever takes free entries or extends a chain's end.
TEST(SdFatAlloc, AllocationTakesOnlyFreeClusters) {
  Card card(4ull << 30);
  ASSERT_EQ(card.volume.fatType(), 32);
  ASSERT_TRUE(card.fill("/fill.bin", 64ull << 20));
  card.device.logWrites = true;
  std::vector<uint8_t> chunk(card.volume.bytesPerCluster() + 7, 3);
  for (int i = 0; i < 40; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "/f%d.bin", i % 8);
    ASSERT_TRUE(card.write(name, chunk.data(), chunk.size(), /*append=*/true));
    if (i % 5 == 4) card.remount();
  }
  card.device.logWrites = false;
  expectAllocationOnly(card, card.device.writes);
  card.check({"fill.bin"});
}

// FAT32: of the reserved sectors only FSInfo is written, and only in its next-free field; the
// backup boot sector and backup FSInfo stay as formatted.
TEST(SdFatAlloc, TheHintChangesOnlyFsInfoNextFree) {
  Card card(4ull << 30);
  ASSERT_EQ(card.volume.fatType(), 32);
  const Sector formatted = card.device.peek(card.fsInfoSector());
  card.device.logWrites = true;
  // 32 MB of clusters moves the search start past the write-back step (512 clusters).
  ASSERT_TRUE(card.fill("/fill.bin", 32ull << 20));
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(card.write(("/s" + std::to_string(i)).c_str(), "s", 1));
  card.remount();
  ASSERT_TRUE(card.fill("/fill2.bin", 32ull << 20));
  card.device.logWrites = false;
  const Sector now = card.device.peek(card.fsInfoSector());
  for (size_t i = 0; i < 512; ++i) {
    if (i >= 492 && i < 496) continue;
    ASSERT_EQ(now[i], formatted[i]) << "FSInfo byte " << i;
  }
  EXPECT_NE(le32(now.data() + 492), le32(formatted.data() + 492)) << "the hint was never written";
  const uint32_t reserved = card.volume.fatStartSector() - card.partitionStart;
  for (const auto& w : card.device.writes) {
    if (w.sector >= card.partitionStart && w.sector < card.partitionStart + reserved) {
      EXPECT_EQ(w.sector, card.fsInfoSector()) << "reserved sector " << w.sector - card.partitionStart;
    }
  }
  card.check({"fill.bin", "fill2.bin"});
}

// FAT16 has no FSInfo: nothing below the FAT is written.
TEST(SdFatAlloc, Fat16WritesNothingBelowTheFat) {
  Card card(64ull << 20);
  ASSERT_EQ(card.volume.fatType(), 16);
  card.device.logWrites = true;
  ASSERT_TRUE(card.fill("/fill.bin", 16ull << 20));
  for (int i = 0; i < 20; ++i) ASSERT_TRUE(card.write(("/s" + std::to_string(i)).c_str(), "s", 1));
  card.remount();
  ASSERT_TRUE(card.write("/after.bin", "a", 1));
  card.device.logWrites = false;
  for (const auto& w : card.device.writes) EXPECT_GE(w.sector, card.volume.fatStartSector());
  card.check({"fill.bin"});
}

// The hint is only a hint: past the card, into used clusters, or in an FSInfo sector whose
// signature is broken (then never written), allocation still finds free clusters only.
TEST(SdFatAlloc, AnyHintStillAllocatesFreeClustersOnly) {
  Card card(4ull << 30);
  ASSERT_EQ(card.volume.fatType(), 32);
  ASSERT_TRUE(card.write("/a.bin", "a", 1));
  const uint32_t last = card.lastCluster();
  int round = 0;
  for (const uint32_t hint : {0xFFFFFFFFu, 2u, 3u, last, last + 1}) {
    card.setHint(hint);
    card.remount();
    ASSERT_TRUE(card.write(("/h" + std::to_string(round++)).c_str(), "h", 1)) << "hint " << hint;
    ASSERT_TRUE(card.fill(("/r" + std::to_string(round)).c_str(), 32ull << 20)) << "hint " << hint;
    card.check({"r1", "r2", "r3", "r4", "r5"});
  }
  // Broken lead signature: the sector is never written again.
  Sector broken = card.device.peek(card.fsInfoSector());
  broken[0] ^= 0xFF;
  card.device.poke(card.fsInfoSector(), broken);
  card.remount();
  card.device.logWrites = true;
  ASSERT_TRUE(card.fill("/big.bin", 64ull << 20));
  ASSERT_TRUE(card.write("/z.bin", "z", 1));
  card.device.logWrites = false;
  for (const auto& w : card.device.writes) EXPECT_NE(w.sector, card.fsInfoSector());
  EXPECT_EQ(card.device.peek(card.fsInfoSector()), broken);
  char back = 0;
  FatFile a;
  ASSERT_TRUE(a.open(&card.volume, "/a.bin", O_RDONLY));
  EXPECT_EQ(a.read(&back, 1), 1);
  EXPECT_EQ(back, 'a');
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
  file.close();
  EXPECT_EQ(card.volume.freeClusterCount(), 0);
  // A contiguous run freed below the search start is found the same way.
  ASSERT_TRUE(card.volume.remove("/big.bin"));
  EXPECT_TRUE(card.fill("/run.bin", 4ull * clusterBytes));
  card.check({"run.bin"});
}

// Many rounds of creating, growing and deleting files on a nearly full FAT32 card, remounting it
// and spoiling the hint, checked like fsck after each round and read back byte for byte.
TEST(SdFatAlloc, RandomRoundsKeepTheCardConsistent) {
  Card card(4ull << 30);
  ASSERT_EQ(card.volume.fatType(), 32);
  const uint32_t clusterBytes = card.volume.bytesPerCluster();
  // Leave some 300 clusters free, in two runs of preallocated space before them.
  const uint64_t fillClusters = card.volume.freeClusterCount() - 300;
  ASSERT_TRUE(card.fill("/fill1.bin", fillClusters / 2 * clusterBytes));
  ASSERT_TRUE(card.fill("/fill2.bin", (fillClusters - fillClusters / 2) * clusterBytes));
  std::mt19937 rng(20260926);
  std::map<std::string, std::vector<uint8_t>> files;
  auto pattern = [](const std::string& name, size_t from, size_t count) {
    std::vector<uint8_t> bytes(count);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(name.size() * 31 + (from + i) * 7);
    return bytes;
  };
  for (int round = 0; round < 400; ++round) {
    const int op = rng() % 10;
    if (op < 4 || files.empty()) {
      const std::string name = "/n" + std::to_string(round) + ".bin";
      const size_t size = 1 + rng() % (clusterBytes * 5 / 2);
      const auto bytes = pattern(name, 0, size);
      if (card.write(name.c_str(), bytes.data(), bytes.size())) {
        files[name] = bytes;
      } else {
        card.volume.remove(name.c_str());  // the card was full
      }
    } else if (op < 7) {
      auto it = std::next(files.begin(), rng() % files.size());
      const size_t size = 1 + rng() % clusterBytes;
      const auto bytes = pattern(it->first, it->second.size(), size);
      FatFile file;
      ASSERT_TRUE(file.open(&card.volume, it->first.c_str(), O_RDWR | O_APPEND));
      const int wrote = file.write(bytes.data(), bytes.size());
      ASSERT_TRUE(file.close());
      if (wrote > 0) it->second.insert(it->second.end(), bytes.begin(), bytes.begin() + wrote);
      // A short write leaves the file at what it reports.
      if (wrote != static_cast<int>(bytes.size())) {
        FatFile check;
        ASSERT_TRUE(check.open(&card.volume, it->first.c_str(), O_RDONLY));
        it->second.resize(check.fileSize());
        check.close();
      }
    } else if (op < 9) {
      auto it = std::next(files.begin(), rng() % files.size());
      ASSERT_TRUE(card.volume.remove(it->first.c_str()));
      files.erase(it);
    } else {
      static const uint32_t hints[] = {0xFFFFFFFFu, 2u, 3u, 0u, 1u};
      const uint32_t choice = rng() % 7;
      card.setHint(choice < 5 ? hints[choice] : choice == 5 ? card.lastCluster() : 2 + rng() % card.lastCluster());
      card.remount();
    }
    std::set<std::string> preallocated = {"fill1.bin", "fill2.bin"};
    card.check(preallocated);
    if (HasFailure()) FAIL() << "round " << round;
  }
  for (const auto& [name, bytes] : files) {
    FatFile file;
    ASSERT_TRUE(file.open(&card.volume, name.c_str(), O_RDONLY)) << name;
    std::vector<uint8_t> back(bytes.size());
    EXPECT_EQ(file.read(back.data(), back.size()), static_cast<int>(back.size())) << name;
    EXPECT_EQ(back, bytes) << name;
    file.close();
  }
}

// Sleep cuts the X3 card's power right after SDCardManager::shutdown(). SdFat still holds a data
// sector and a FAT sector in its caches then (a write not yet closed); FsVolume::end() does not
// write them, syncSdVolume does.
TEST(SdFatAlloc, SyncBeforePowerCutPutsCachedSectorsOnTheCard) {
  for (const bool synced : {false, true}) {
    Card card(64ull << 20);
    FatFile file;
    ASSERT_TRUE(file.open(&card.volume, "/state.json", O_RDWR | O_CREAT));
    ASSERT_EQ(file.write("abc", 3), 3);  // part of a sector: it stays in the data cache
    const uint32_t cluster = file.firstCluster();
    ASSERT_GE(cluster, 2u);
    if (synced) EXPECT_TRUE(syncSdVolume(card.volume));
    // The power cut: nothing reaches the card after this point.
    const Sector data =
        card.device.peek(card.volume.dataStartSector() + (cluster - 2) * card.volume.sectorsPerCluster());
    const Sector fat = card.device.peek(card.volume.fatStartSector() + cluster * 2 / 512);
    const uint32_t entry = fat[cluster * 2 % 512] | fat[cluster * 2 % 512 + 1] << 8;
    EXPECT_EQ(memcmp(data.data(), "abc", 3) == 0, synced) << "data sector, synced=" << synced;
    EXPECT_EQ(entry != 0, synced) << "FAT entry, synced=" << synced;
  }
}
