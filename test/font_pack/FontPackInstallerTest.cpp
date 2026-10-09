#include <FontPackInstaller.h>
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
std::vector<Entry> entries(uint8_t raster = 1, bool badSha = false, bool badLayout = false) {
  std::vector<Entry> result;
  const auto signature = layout(font(raster));
  std::string metadata = "{\"format\":1,\"cpfont_version\":4,\"family\":\"Example\","
                         "\"recipe\":\"reader-outline-v2-step32\",\"levels\":[";
  for (int level = 0; level < 6; ++level) {
    if (level) metadata += ',';
    metadata += "{\"level\":" + std::to_string(level) + ",\"strength_26_6\":" + std::to_string(level * 32) +
                ",\"directory\":\"" + (level ? "weight-" + std::to_string(level + 1) : "") + "\"}";
    result.push_back({(level ? "weight-" + std::to_string(level + 1) + "/" : "") + "Example_14.cpfont",
                      font(raster + level, badLayout && level == 5 ? 161 : 160)});
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
