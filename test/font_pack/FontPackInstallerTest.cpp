#include <FontPackInstaller.h>
#include <ReaderInkWeight.h>
#include <HalStorage.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <vector>
#include <unistd.h>
#include "activities/settings/FontDownloadTransfer.h"

namespace {
using Bytes = std::vector<uint8_t>;
void put16(Bytes& bytes, size_t offset, uint16_t value) {
  bytes[offset] = value; bytes[offset + 1] = value >> 8;
}
void put32(Bytes& bytes, size_t offset, uint32_t value) {
  put16(bytes, offset, value); put16(bytes, offset + 2, value >> 16);
}
uint32_t crc(const Bytes& bytes) {
  uint32_t value = 0xffffffff;
  for (uint8_t byte : bytes) {
    value ^= byte;
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  }
  return ~value;
}
std::string sha(const Bytes& bytes) {
  uint8_t digest[32]; SHA256(bytes.data(), bytes.size(), digest);
  std::string result;
  for (uint8_t byte : digest) { char pair[3]; std::snprintf(pair, sizeof(pair), "%02x", byte); result += pair; }
  return result;
}
Bytes font(uint8_t raster, uint16_t advance = 160) {
  Bytes bytes(172, 0);
  std::memcpy(bytes.data(), "CPFONT\0\0", 8);
  put16(bytes, 8, 4); put16(bytes, 10, 1); bytes[12] = 1;
  put32(bytes, 36, 1); put32(bytes, 40, 3);
  bytes[44] = 12; put16(bytes, 45, 10); put16(bytes, 47, -2);
  put32(bytes, 56, 64); put16(bytes, 60, 10);
  put32(bytes, 64, 'A'); put32(bytes, 68, 'C');
  for (uint32_t index = 0; index < 3; ++index) {
    const auto offset = 76 + index * 16;
    bytes[offset] = 8; bytes[offset + 1] = 8; put16(bytes, offset + 2, advance);
    put16(bytes, offset + 6, 8); put16(bytes, offset + 8, 16); put32(bytes, offset + 12, index * 16);
  }
  std::fill(bytes.begin() + 124, bytes.end(), raster);
  return bytes;
}
std::string layout(const Bytes& bytes) {
  Bytes canonical(bytes.begin(), bytes.begin() + 64);
  put32(canonical, 56, 0);
  canonical.insert(canonical.end(), bytes.begin() + 64, bytes.begin() + 76);
  for (uint32_t index = 0; index < 3; ++index) {
    const auto offset = canonical.size(); canonical.resize(offset + 6);
    put32(canonical, offset, 'A' + index);
    put16(canonical, offset + 4, bytes[78 + index * 16] | (bytes[79 + index * 16] << 8));
  }
  return sha(canonical);
}
struct Entry { std::string name; Bytes bytes; uint16_t method = 0; bool badCrc = false; };
Bytes archive(std::vector<Entry> entries) {
  Bytes output, central;
  for (const auto& entry : entries) {
    const uint32_t checksum = crc(entry.bytes) ^ (entry.badCrc ? 0x1234 : 0);
    const uint32_t offset = output.size();
    Bytes local(30, 0); put32(local, 0, 0x04034b50); put16(local, 4, 20);
    put16(local, 8, entry.method); put32(local, 14, checksum);
    put32(local, 18, entry.bytes.size()); put32(local, 22, entry.bytes.size()); put16(local, 26, entry.name.size());
    output.insert(output.end(), local.begin(), local.end());
    output.insert(output.end(), entry.name.begin(), entry.name.end());
    output.insert(output.end(), entry.bytes.begin(), entry.bytes.end());
    Bytes record(46, 0); put32(record, 0, 0x02014b50); put16(record, 4, 20); put16(record, 6, 20);
    put16(record, 10, entry.method); put32(record, 16, checksum);
    put32(record, 20, entry.bytes.size()); put32(record, 24, entry.bytes.size()); put16(record, 28, entry.name.size());
    put32(record, 38, 0x81a40000); put32(record, 42, offset);
    central.insert(central.end(), record.begin(), record.end());
    central.insert(central.end(), entry.name.begin(), entry.name.end());
  }
  const auto centralOffset = output.size(); output.insert(output.end(), central.begin(), central.end());
  Bytes end(22, 0); put32(end, 0, 0x06054b50); put16(end, 8, entries.size()); put16(end, 10, entries.size());
  put32(end, 12, central.size()); put32(end, 16, centralOffset);
  output.insert(output.end(), end.begin(), end.end());
  return output;
}
// A pack level is the physical weight minus 1: 0 the base file, 1..5 the +1..+5 files, 6..8 the -1..-3 files.
// Recipes v3-ramp and v4-thin (to v1.0.68) carry the earlier strengths, v5-even the current table.
int packStrength(int level, bool legacy = false) {
  const uint8_t stored = readerInk::publicFromPhysical(level + 1);
  return legacy ? readerInk::legacyOutlineStrength(stored) : readerInk::outlineStrength(stored);
}
enum class Fault { None, ThinStrengthSign, ThinDirectory, ThinLevelMissing, NineLevelsOldRecipe, SixLevelsThinRecipe };
std::vector<Entry> entries(uint8_t raster = 1, bool badSha = false, bool badLayout = false,
                           bool oldRecipe = false, bool oldStrengths = false, int levels = 9,
                           Fault fault = Fault::None, bool v4 = false) {
  std::vector<Entry> result;
  const auto signature = layout(font(raster));
  std::string metadata = "{\"format\":1,\"cpfont_version\":4,\"family\":\"Example\","
                         "\"recipe\":\"";
  const bool thinRecipe = fault == Fault::SixLevelsThinRecipe || (levels == 9 && fault != Fault::NineLevelsOldRecipe);
  metadata += oldRecipe     ? "reader-outline-v2-step32"
              : !thinRecipe ? "reader-outline-v3-ramp"
              : v4          ? "reader-outline-v4-thin"
                            : "reader-outline-v5-even";
  const bool legacy = v4 || !thinRecipe;
  metadata += "\",\"levels\":[";
  for (int level = 0; level < levels; ++level) {
    if (level) metadata += ',';
    const int strength = oldStrengths ? level * 32
                         : fault == Fault::ThinStrengthSign && level == 6 ? -packStrength(level, legacy)
                                                                          : packStrength(level, legacy);
    const std::string directory = fault == Fault::ThinDirectory && level == 6 ? "weight-6"
                                  : level ? "weight-" + std::to_string(level + 1) : "";
    metadata += "{\"level\":" + std::to_string(level) + ",\"strength_26_6\":" + std::to_string(strength) +
                ",\"directory\":\"" + directory + "\"}";
    if (fault == Fault::ThinLevelMissing && level == 8) {
      metadata.erase(metadata.rfind(",{"));  // the 9 level recipe with only 8 levels listed
      continue;
    }
    result.push_back({(level ? "weight-" + std::to_string(level + 1) + "/" : "") + "Example_14.cpfont",
                      font(raster + level, badLayout && level == levels - 1 ? 161 : 160)});
  }
  result.push_back({"OFL.txt", Bytes{'O', 'F', 'L'}});
  metadata += "],\"layout_signatures\":{\"14\":\"" + signature + "\"},\"entries\":[";
  for (size_t index = 0; index < result.size(); ++index) {
    if (index) metadata += ',';
    const auto& entry = result[index];
    metadata += "{\"path\":\"" + entry.name + "\",\"size\":" + std::to_string(entry.bytes.size()) +
                ",\"sha256\":\"" + (badSha && index == 0 ? std::string(64, '0') : sha(entry.bytes)) + "\"";
    if (entry.name != "OFL.txt") metadata += ",\"layout_signature\":\"" + signature + "\"";
    metadata += '}';
  }
  metadata += "]}"; result.push_back({"pack.json", Bytes(metadata.begin(), metadata.end())});
  return result;
}
class FontPack : public testing::Test {
 protected:
  void SetUp() override {
    cardRoot = std::filesystem::temp_directory_path() / ("font-pack-" + std::to_string(getpid()));
    std::filesystem::remove_all(cardRoot); std::filesystem::create_directories(cardRoot / "fonts/Example");
    cutDuringStage = cutAfterBackup = failPromotion = false; Storage.available = 1ULL << 30;
    failBackupCleanup = failPackCleanup = false;
    std::ofstream(cardRoot / "fonts/Example/old.txt", std::ios::binary) << "original family";
  }
  void TearDown() override { std::filesystem::remove_all(cardRoot); }
  void package(std::vector<Entry> payload = entries()) {
    const auto bytes = archive(std::move(payload));
    std::ofstream stream(cardRoot / "fonts/Example.cpfontpack", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  std::string read(const char* path) {
    std::ifstream stream(Storage.path(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  }
  void unchanged() {
    EXPECT_EQ(read("/fonts/Example/old.txt"), "original family");
    EXPECT_TRUE(Storage.exists("/fonts/Example.cpfontpack"));
  }
};
}

TEST_F(FontPack, ValidStoreInstallsEveryEntryAndDeletesOnlyCommittedPackage) {
  package(); ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  for (const auto& entry : entries()) EXPECT_EQ(read(("/fonts/Example/" + entry.name).c_str()),
                                                std::string(entry.bytes.begin(), entry.bytes.end()));
  EXPECT_FALSE(Storage.exists("/fonts/Example.old")); EXPECT_FALSE(Storage.exists("/fonts/Example.cpfontpack"));
  EXPECT_LE(FontPackInstaller::workingSetBytes(), 8192u);
  std::printf("installer_working_set=%zu\n", FontPackInstaller::workingSetBytes());
}
TEST_F(FontPack, CorruptCrcKeepsOldFamilyAndPackage) {
  auto payload = entries(); payload[0].badCrc = true; package(payload);
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK); unchanged();
}
TEST_F(FontPack, OldRecipeKeepsOldFamilyAndPackage) {
  const auto installed = font(42);
  std::filesystem::create_directories(cardRoot / "fonts/Example/weight-1");
  std::ofstream stream(cardRoot / "fonts/Example/weight-1/Example_14.cpfont", std::ios::binary);
  stream.write(reinterpret_cast<const char*>(installed.data()), installed.size());
  stream.close();
  package(entries(1, false, false, true, true));
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK);
  unchanged();
  EXPECT_FALSE(Storage.exists("/fonts/Example/.staging"));
  EXPECT_EQ(read("/fonts/Example/weight-1/Example_14.cpfont"),
            std::string(installed.begin(), installed.end()));
}
TEST_F(FontPack, OldStrengthsWithNewRecipeKeepOldFamilyAndPackage) {
  package(entries(1, false, false, false, true));
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK);
  unchanged();
  EXPECT_FALSE(Storage.exists("/fonts/Example/.staging"));
}
TEST_F(FontPack, NineLevelPackInstallsEveryWeightDirectoryIncludingTheThinOnes) {
  package(entries());
  ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  for (int weight = 2; weight <= 9; ++weight) {
    const auto expected = font(1 + weight - 1);
    EXPECT_EQ(read(("/fonts/Example/weight-" + std::to_string(weight) + "/Example_14.cpfont").c_str()),
              std::string(expected.begin(), expected.end()));
  }
}
TEST_F(FontPack, NineLevelPackFromV1068StillInstallsWithItsOwnStrengths) {
  package(entries(1, false, false, false, false, 9, Fault::None, /*v4=*/true));
  ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  EXPECT_TRUE(Storage.exists("/fonts/Example/weight-9/Example_14.cpfont"));
}
TEST_F(FontPack, EachRecipeChecksItsOwnStrengthTable) {
  // A v4-thin pack listing the current strengths, or a v5-even one listing the earlier ones, is not one the
  // builder wrote.
  for (const bool v4 : {false, true}) {
    SCOPED_TRACE(v4);
    auto files = entries(1, false, false, false, false, 9, Fault::None, v4);
    ASSERT_EQ(files.back().name, "pack.json");
    std::string metadata(files.back().bytes.begin(), files.back().bytes.end());
    const std::string from = v4 ? std::to_string(packStrength(1, true)) : std::to_string(packStrength(1));
    const std::string to = v4 ? std::to_string(packStrength(1)) : std::to_string(packStrength(1, true));
    const auto at = metadata.find("\"strength_26_6\":" + from + ",");
    ASSERT_NE(at, std::string::npos);
    metadata.replace(at + 16, from.size(), to);
    files.back().bytes = Bytes(metadata.begin(), metadata.end());
    package(files);
    EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK);
    unchanged();
  }
}
// build_font_pack.py writes pack.json with sorted keys: "levels" comes before "recipe".
std::vector<Entry> recipeLast(std::vector<Entry> files) {
  std::string metadata(files.back().bytes.begin(), files.back().bytes.end());
  const auto start = metadata.find(",\"recipe\":\"");
  const auto end = metadata.find('"', start + 11) + 1;
  const std::string recipe = metadata.substr(start, end - start);
  metadata.erase(start, end - start);
  metadata.insert(metadata.rfind('}'), recipe);
  files.back().bytes = Bytes(metadata.begin(), metadata.end());
  return files;
}
TEST_F(FontPack, PackWithTheRecipeAfterItsLevelsChecksTheRightTable) {
  for (const bool v4 : {false, true}) {
    SCOPED_TRACE(v4);
    package(recipeLast(entries(1, false, false, false, false, 9, Fault::None, v4)));
    EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  }
  // A v5-even pack listing the earlier strengths is still refused when its recipe comes last.
  auto files = recipeLast(entries());
  std::string metadata(files.back().bytes.begin(), files.back().bytes.end());
  const std::string from = std::to_string(packStrength(1)), to = std::to_string(packStrength(1, true));
  const auto at = metadata.find("\"strength_26_6\":" + from + ",");
  ASSERT_NE(at, std::string::npos);
  metadata.replace(at + 16, from.size(), to);
  files.back().bytes = Bytes(metadata.begin(), metadata.end());
  package(files);
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK);
}
TEST_F(FontPack, SixLevelPackFromTheEarlierReleaseStillInstalls) {
  package(entries(1, false, false, false, false, 6));
  ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  EXPECT_TRUE(Storage.exists("/fonts/Example/weight-6/Example_14.cpfont"));
  EXPECT_FALSE(Storage.exists("/fonts/Example/weight-7/Example_14.cpfont"));
  EXPECT_FALSE(Storage.exists("/fonts/Example.cpfontpack"));
}
TEST_F(FontPack, ThinLevelsNeedTheirRecipeAndTheirOwnStrengthsAndDirectories) {
  for (const Fault fault : {Fault::ThinStrengthSign, Fault::ThinDirectory, Fault::ThinLevelMissing,
                            Fault::NineLevelsOldRecipe, Fault::SixLevelsThinRecipe}) {
    SCOPED_TRACE(static_cast<int>(fault));
    package(entries(1, false, false, false, false, fault == Fault::SixLevelsThinRecipe ? 6 : 9, fault));
    EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK);
    unchanged();
    EXPECT_FALSE(Storage.exists("/fonts/Example/.staging"));
  }
}
TEST_F(FontPack, CompressedEntryKeepsOldFamilyAndPackage) {
  auto payload = entries(); payload[0].method = 8; package(payload);
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK); unchanged();
}
TEST_F(FontPack, TrailingManifestDataKeepsOldFamilyAndPackage) {
  auto payload = entries();
  const std::string trailing = " true";
  payload.back().bytes.insert(payload.back().bytes.end(), trailing.begin(), trailing.end());
  package(payload);
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK);
  unchanged();
}
TEST_F(FontPack, TraversalEntryKeepsOldFamilyAndPackage) {
  auto payload = entries(); payload[0].name = "../Example_14.cpfont"; package(payload);
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK); unchanged();
}
TEST_F(FontPack, InvalidFamilyDoesNotTouchOldFamily) {
  package(); EXPECT_FALSE(FontPackInstaller::isPackFilename("../Example.cpfontpack"));
  EXPECT_FALSE(FontPackInstaller::isPackFilename("Bad.Name.cpfontpack"));
  EXPECT_TRUE(FontPackInstaller::isPackFilename("Example.cpfontpack"));
  EXPECT_EQ(FontPackInstaller::install("/fonts/../Example.cpfontpack"), FontPackInstaller::Result::INVALID_NAME); unchanged();
}
TEST_F(FontPack, ShaMismatchAndDifferentLayoutKeepOldFamily) {
  package(entries(1, true)); EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK); unchanged();
  package(entries(1, false, true)); EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK); unchanged();
}
TEST_F(FontPack, NoSpaceStopsBeforeStaging) {
  package(); Storage.available = 1024;
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::NO_SPACE); unchanged();
  EXPECT_FALSE(Storage.exists("/fonts/Example/.staging"));
}
TEST_F(FontPack, PowerCutDuringStagingLeavesOldFamilyAndAllowsRetry) {
  package(); cutDuringStage = true;
  EXPECT_THROW(FontPackInstaller::install("/fonts/Example.cpfontpack"), PowerCut); unchanged();
  cutDuringStage = false;
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
}
TEST_F(FontPack, PowerCutAfterBackupRestoresOldBeforeRetry) {
  package(); cutAfterBackup = true;
  EXPECT_THROW(FontPackInstaller::install("/fonts/Example.cpfontpack"), PowerCut);
  cutAfterBackup = false; ASSERT_TRUE(FontPackInstaller::recover("Example")); unchanged();
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
}
TEST_F(FontPack, PromotionFailureRollsBackOriginalFamily) {
  package(); failPromotion = true;
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::IO_ERROR); unchanged();
}
TEST_F(FontPack, ConsecutiveInstallsReplaceWithoutLeavingOldOrStage) {
  package(); ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  package(entries(20)); ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  const auto expected = font(25);
  EXPECT_EQ(read("/fonts/Example/weight-6/Example_14.cpfont"), std::string(expected.begin(), expected.end()));
  EXPECT_FALSE(Storage.exists("/fonts/Example.old")); EXPECT_FALSE(Storage.exists("/fonts/Example/.staging"));
}
TEST_F(FontPack, DuplicateEntriesKeepOldFamily) {
  auto payload = entries(); payload.push_back(payload.front()); package(payload);
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::INVALID_PACK); unchanged();
}
TEST_F(FontPack, ExistingHiddenFamilyStaysInItsRoot) {
  std::filesystem::rename(cardRoot / "fonts/Example", cardRoot / "Example");
  std::filesystem::create_directories(cardRoot / ".fonts");
  std::filesystem::rename(cardRoot / "Example", cardRoot / ".fonts/Example");
  package(); EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  EXPECT_TRUE(Storage.exists("/.fonts/Example/weight-6/Example_14.cpfont"));
  EXPECT_FALSE(Storage.exists("/fonts/Example"));
}
TEST_F(FontPack, PublishedPackUsesTheSameProductionInstaller) {
  const char* source = std::getenv("CROSSPOINT_FONT_PACK_REAL");
  if (!source) GTEST_SKIP();
  const auto input = std::filesystem::path(source);
  const auto target = cardRoot / "fonts" / input.filename();
  std::filesystem::copy_file(input, target);
  const auto started = std::chrono::steady_clock::now();
  ASSERT_EQ(FontPackInstaller::install(("/fonts/" + input.filename().string()).c_str()), FontPackInstaller::Result::OK);
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  EXPECT_TRUE(std::filesystem::exists(cardRoot / "fonts" / input.stem() / "pack.json"));
  EXPECT_FALSE(std::filesystem::exists(target));
  std::printf("real_pack=%s bytes=%llu install_seconds=%.6f workspace=%zu\n", input.filename().c_str(),
              static_cast<unsigned long long>(std::filesystem::file_size(input)), elapsed, FontPackInstaller::workingSetBytes());
}

TEST_F(FontPack, ManifestFileClassifier) {
  using fontdownload::ManifestFileKind;
  EXPECT_EQ(fontdownload::classifyManifestFile("Example.cpfontpack"), ManifestFileKind::Pack);
  EXPECT_EQ(fontdownload::classifyManifestFile("Example_14.cpfont"), ManifestFileKind::SingleFont);
  EXPECT_EQ(fontdownload::classifyManifestFile("weight-6/Example_14.cpfont"), ManifestFileKind::SingleFont);
  for (const char* name : {"../Example.cpfontpack", "weight-6/Example.cpfontpack", "Example.ttf", "", "X.CPFONTPACK"})
    EXPECT_EQ(fontdownload::classifyManifestFile(name), ManifestFileKind::Invalid);
  EXPECT_EQ(fontdownload::classifyManifestFile(nullptr), ManifestFileKind::Invalid);
  EXPECT_TRUE(fontdownload::packMatchesFamily("Example.cpfontpack", "Example"));
  EXPECT_FALSE(fontdownload::packMatchesFamily("Other.cpfontpack", "Example"));
  EXPECT_EQ(fontdownload::requiredPackSpace(68363915), 140922134u);
  EXPECT_TRUE(fontdownload::canInstallPack(10240 + 5512 + 1024, 8180 + 5512 + 1024, 5512));
  EXPECT_FALSE(fontdownload::canInstallPack(10240 + 5512 + 1023, 8180 + 5512 + 1024, 5512));
  EXPECT_FALSE(fontdownload::canInstallPack(10240 + 5512 + 1024, 8180 + 5512 + 1023, 5512));
}

TEST_F(FontPack, StreamCallsRealInstallerOnceAfterClosingTransport) {
  const auto bytes = archive(entries());
  bool cancelled = false;
  bool transportClosed = false;
  bool markedInstalled = false;
  unsigned installs = 0;
  const auto result = fontdownload::downloadAndInstallPack(
      "Example.cpfontpack", bytes.size(), crc(bytes), cancelled,
      [&](const auto& sink) {
        for (size_t offset = 0; offset < bytes.size();) {
          const size_t count = std::min<size_t>(1024, bytes.size() - offset);
          if (!sink(bytes.data() + offset, count)) return false;
          offset += count;
        }
        transportClosed = true;
        return true;
      },
      [&](const char* path) {
        ++installs;
        EXPECT_TRUE(transportClosed);
        EXPECT_FALSE(markedInstalled);
        EXPECT_FALSE(Storage.exists("/fonts/Example.cpfontpack.tmp"));
        EXPECT_TRUE(Storage.exists("/fonts/Example/old.txt"));
        return FontPackInstaller::install(path) == FontPackInstaller::Result::OK;
      });
  if (result == fontdownload::TransferResult::OK) markedInstalled = true;
  EXPECT_EQ(result, fontdownload::TransferResult::OK);
  EXPECT_EQ(installs, 1u);
  EXPECT_TRUE(markedInstalled);
  EXPECT_TRUE(Storage.exists("/fonts/Example/weight-6/Example_14.cpfont"));
  EXPECT_FALSE(Storage.exists("/fonts/Example/old.txt"));
}

TEST_F(FontPack, BadStreamCrcNeverCallsInstaller) {
  const auto bytes = archive(entries());
  bool cancelled = false;
  unsigned installs = 0;
  const auto result = fontdownload::downloadAndInstallPack(
      "Example.cpfontpack", bytes.size(), crc(bytes) ^ 1, cancelled,
      [&](const auto& sink) { return sink(bytes.data(), bytes.size()); },
      [&](const char*) { ++installs; return true; });
  EXPECT_EQ(result, fontdownload::TransferResult::ChecksumError);
  EXPECT_EQ(installs, 0u);
  EXPECT_FALSE(Storage.exists("/fonts/Example.cpfontpack.tmp"));
  EXPECT_FALSE(Storage.exists("/fonts/Example.cpfontpack"));
  EXPECT_EQ(read("/fonts/Example/old.txt"), "original family");
}

TEST_F(FontPack, RealInstallerRollbackKeepsFamilyAndRetainsDownloadedPack) {
  const auto bytes = archive(entries());
  bool cancelled = false;
  failPromotion = true;
  unsigned installs = 0;
  const auto result = fontdownload::downloadAndInstallPack(
      "Example.cpfontpack", bytes.size(), crc(bytes), cancelled,
      [&](const auto& sink) { return sink(bytes.data(), bytes.size()); },
      [&](const char* path) { ++installs; return FontPackInstaller::install(path) == FontPackInstaller::Result::OK; });
  EXPECT_EQ(result, fontdownload::TransferResult::InstallError);
  EXPECT_EQ(installs, 1u);
  EXPECT_FALSE(Storage.exists("/fonts/Example.cpfontpack.tmp"));
  EXPECT_TRUE(Storage.exists("/fonts/Example.cpfontpack"));
  unchanged();
}

TEST_F(FontPack, SpaceCheckRejectsBeforeOpeningTransport) {
  bool cancelled = false;
  bool fetched = false;
  bool installed = false;
  Storage.available = fontdownload::requiredPackSpace(68363915) - 1;
  std::ofstream(cardRoot / "fonts/Example.cpfontpack.tmp") << "interrupted download";
  const auto result = fontdownload::downloadAndInstallPack(
      "Example.cpfontpack", 68363915, 0, cancelled,
      [&](const auto&) { fetched = true; return true; },
      [&](const char*) { installed = true; return true; });
  EXPECT_EQ(result, fontdownload::TransferResult::NoSpace);
  EXPECT_FALSE(fetched);
  EXPECT_FALSE(installed);
  EXPECT_FALSE(Storage.exists("/fonts/Example.cpfontpack.tmp"));
  EXPECT_EQ(read("/fonts/Example/old.txt"), "original family");
}

TEST_F(FontPack, MidStreamCancellationRemovesOnlyTemporary) {
  const auto bytes = archive(entries());
  bool cancelled = false;
  bool installed = false;
  const auto result = fontdownload::downloadAndInstallPack(
      "Example.cpfontpack", bytes.size(), crc(bytes), cancelled,
      [&](const auto& sink) {
        EXPECT_TRUE(sink(bytes.data(), 17));
        cancelled = true;
        return sink(bytes.data() + 17, bytes.size() - 17);
      },
      [&](const char*) { installed = true; return true; });
  EXPECT_EQ(result, fontdownload::TransferResult::Aborted);
  EXPECT_FALSE(installed);
  EXPECT_FALSE(Storage.exists("/fonts/Example.cpfontpack.tmp"));
  EXPECT_EQ(read("/fonts/Example/old.txt"), "original family");
}

TEST_F(FontPack, CleanupFailureReportsTheAlreadyCommittedInstall) {
  package();
  failBackupCleanup = true;
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  EXPECT_TRUE(Storage.exists("/fonts/Example/.pack-ready"));
  EXPECT_TRUE(Storage.exists("/fonts/Example.old/old.txt"));
  EXPECT_FALSE(Storage.exists("/fonts/Example/old.txt"));
  failBackupCleanup = false;
  EXPECT_TRUE(FontPackInstaller::recover("Example"));
  EXPECT_FALSE(Storage.exists("/fonts/Example.old"));
}

TEST_F(FontPack, PackageCleanupFailureReportsCommittedFamily) {
  package();
  failPackCleanup = true;
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  EXPECT_TRUE(Storage.exists("/fonts/Example/.pack-ready"));
  EXPECT_TRUE(Storage.exists("/fonts/Example.cpfontpack"));
  EXPECT_FALSE(Storage.exists("/fonts/Example/old.txt"));
}

TEST_F(FontPack, ReceiptTracksVerifiedCatalogAndExactRoot) {
  const auto bytes = archive(entries());
  package();
  ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack", bytes.size(), crc(bytes)), FontPackInstaller::Result::OK);
  EXPECT_TRUE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size(), crc(bytes)));
  EXPECT_FALSE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size(), crc(bytes) ^ 1));
  EXPECT_FALSE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size() + 1, crc(bytes)));
  EXPECT_FALSE(FontPackInstaller::matchesInstalled("/.fonts", "Example", bytes.size(), crc(bytes)));
  EXPECT_EQ(read("/fonts/Example/.pack-ready").size(), 12u);
  EXPECT_FALSE(Storage.exists("/fonts/Example/.pack-ready.tmp"));
}

TEST_F(FontPack, ReceiptMissingNeedsOnlyOneVerifiedInstall) {
  const auto bytes = archive(entries());
  std::ofstream(cardRoot / "fonts/Example/.pack-ready", std::ios::binary).put(1);
  EXPECT_FALSE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size(), crc(bytes)));
  package();
  ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack", bytes.size(), crc(bytes)), FontPackInstaller::Result::OK);
  EXPECT_TRUE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size(), crc(bytes)));
}

TEST_F(FontPack, ReceiptStaysWithOldFamilyAcrossInterruptedInstall) {
  const auto bytes = archive(entries());
  for (unsigned fault = 0; fault < 5; ++fault) {
    SCOPED_TRACE(fault);
    const uint32_t oldReceipt[] = {0x31525046, 100, 3};
    {
      std::ofstream marker(cardRoot / "fonts/Example/.pack-ready", std::ios::binary);
      marker.write(reinterpret_cast<const char*>(oldReceipt), sizeof(oldReceipt));
    }
    package();
    cutDuringStage = fault == 0;
    cutAfterBackup = fault == 1;
    failPromotion = fault == 2;
    failWriteOffset = fault == 3 ? 0 : static_cast<size_t>(-1);
    failReceiptRename = fault == 4;
    if (fault < 2) EXPECT_THROW(FontPackInstaller::install("/fonts/Example.cpfontpack", bytes.size(), crc(bytes)), PowerCut);
    else EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack", bytes.size(), crc(bytes)), FontPackInstaller::Result::IO_ERROR);
    cutDuringStage = cutAfterBackup = failPromotion = failReceiptRename = false;
    failWriteOffset = static_cast<size_t>(-1);
    ASSERT_TRUE(FontPackInstaller::recover("Example"));
    EXPECT_TRUE(FontPackInstaller::matchesInstalled("/fonts", "Example", 100, 3));
    EXPECT_FALSE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size(), crc(bytes)));
    unchanged();
  }
}

TEST_F(FontPack, ReceiptRejectsShortWrongAndUnverifiedRecords) {
  const auto bytes = archive(entries());
  package();
  EXPECT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack", bytes.size() + 1, crc(bytes)), FontPackInstaller::Result::INVALID_PACK);
  unchanged();
  ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack"), FontPackInstaller::Result::OK);
  EXPECT_FALSE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size(), crc(bytes)));
  for (const std::string& value : {std::string(11, 'x'), std::string(12, 'x'), std::string(13, 'x')}) {
    { std::ofstream marker(cardRoot / "fonts/Example/.pack-ready", std::ios::binary); marker << value; }
    EXPECT_FALSE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size(), crc(bytes)));
  }
}

TEST_F(FontPack, ReceiptFollowsHiddenFamilyAndCommittedBackupCleanup) {
  std::filesystem::create_directories(cardRoot / ".fonts");
  std::filesystem::rename(cardRoot / "fonts/Example", cardRoot / ".fonts/Example");
  const auto bytes = archive(entries());
  package(); failBackupCleanup = true;
  ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack", bytes.size(), crc(bytes)), FontPackInstaller::Result::OK);
  EXPECT_TRUE(FontPackInstaller::matchesInstalled("/.fonts", "Example", bytes.size(), crc(bytes)));
  EXPECT_TRUE(Storage.exists("/.fonts/Example.old/old.txt"));
  failBackupCleanup = false;
  ASSERT_TRUE(FontPackInstaller::recover("Example"));
  EXPECT_TRUE(FontPackInstaller::matchesInstalled("/.fonts", "Example", bytes.size(), crc(bytes)));
}

TEST_F(FontPack, ReceiptReadsTemporaryThenBackupOnlyWhenPrimaryIsAbsent) {
  const auto bytes = archive(entries());
  package();
  ASSERT_EQ(FontPackInstaller::install("/fonts/Example.cpfontpack", bytes.size(), crc(bytes)), FontPackInstaller::Result::OK);
  const auto marker = cardRoot / "fonts/Example/.pack-ready";
  std::filesystem::rename(marker, marker.string() + ".tmp");
  EXPECT_TRUE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size(), crc(bytes)));
  std::filesystem::rename(marker.string() + ".tmp", marker.string() + ".bak");
  EXPECT_TRUE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size(), crc(bytes)));
  std::ofstream(marker).put(1);
  EXPECT_FALSE(FontPackInstaller::matchesInstalled("/fonts", "Example", bytes.size(), crc(bytes)));
}
