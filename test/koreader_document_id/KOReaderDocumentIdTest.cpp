#include <gtest/gtest.h>
#include <openssl/evp.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "KOReaderSync/KOReaderDocumentId.h"

namespace {
using Bytes = std::vector<uint8_t>;
const std::string kSample = "META-INF/tenor-koreader-sample.bin";

std::string md5(const Bytes& data) {
  unsigned char digest[16];
  unsigned int length = 0;
  EVP_Digest(data.data(), data.size(), digest, &length, EVP_md5(), nullptr);
  std::string hex;
  char two[3];
  for (unsigned i = 0; i < length; i++) { std::snprintf(two, sizeof(two), "%02x", digest[i]); hex += two; }
  return hex;
}

// KOReader's partialMD5, written out independently of the code under test.
std::string partialMd5(const Bytes& file) {
  Bytes picked;
  for (int i = -1; i <= 10; i++) {
    const size_t offset = i < 0 ? 0 : size_t{1024} << (2 * i);
    if (offset >= file.size()) break;
    picked.insert(picked.end(), file.begin() + offset, file.begin() + std::min(file.size(), offset + 1024));
  }
  return md5(picked);
}

void put16(Bytes& b, uint16_t v) { b.push_back(v & 0xff); b.push_back(v >> 8); }
void put32(Bytes& b, uint32_t v) { put16(b, v & 0xffff); put16(b, v >> 16); }

void local(Bytes& out, const std::string& name, const Bytes& data, uint16_t method = 0, uint16_t flags = 0,
           uint32_t sizeOverride = 0) {
  put32(out, 0x04034b50);
  put16(out, 20);
  put16(out, flags);
  put16(out, method);
  put32(out, 0);  // time, date
  put32(out, 0);  // crc, not checked by the reader
  const uint32_t size = sizeOverride ? sizeOverride : static_cast<uint32_t>(data.size());
  put32(out, size);
  put32(out, size);
  put16(out, static_cast<uint16_t>(name.size()));
  put16(out, 0);
  out.insert(out.end(), name.begin(), name.end());
  out.insert(out.end(), data.begin(), data.end());
}

Bytes pattern(size_t n, uint8_t seed) {
  Bytes b(n);
  for (size_t i = 0; i < n; i++) b[i] = static_cast<uint8_t>(i * 31 + seed);
  return b;
}

std::string hashOf(const Bytes& file) {
  // One file per test: ctest runs the cases of this binary in parallel.
  const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
  const auto path = std::filesystem::temp_directory_path() / (std::string("kodoc_") + info->name() + ".epub");
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  return KOReaderDocumentId::calculate(path.string());
}

Bytes book(const std::string& secondName, const Bytes& second, uint16_t method = 0, uint16_t flags = 0) {
  Bytes out;
  const std::string mime = "application/epub+zip";
  local(out, "mimetype", Bytes(mime.begin(), mime.end()));
  local(out, secondName, second, method, flags);
  local(out, "OEBPS/content.opf", pattern(300000, 7));
  return out;
}
}  // namespace

TEST(KOReaderDocumentId, EmbeddedSampleHashesAsTheOriginal) {
  const Bytes original = pattern(5'000'000, 3);
  Bytes sample;
  for (int i = -1; i <= 10; i++) {
    const size_t offset = i < 0 ? 0 : size_t{1024} << (2 * i);
    if (offset >= original.size()) break;
    sample.insert(sample.end(), original.begin() + offset, original.begin() + std::min(original.size(), offset + 1024));
  }
  const Bytes optimised = book(kSample, sample);
  EXPECT_EQ(hashOf(optimised), partialMd5(original));
  EXPECT_NE(hashOf(optimised), partialMd5(optimised));
}

TEST(KOReaderDocumentId, RejectedSamplesFallBackToTheFileItself) {
  const Bytes sample = pattern(4096, 9);
  for (const Bytes& file : {book(kSample, sample, 8), book(kSample, sample, 0, 0x0008), book("META-INF/other.bin", sample),
                            book(kSample, pattern(12 * 1024 + 1, 9)), book(kSample, Bytes{})}) {
    EXPECT_EQ(hashOf(file), partialMd5(file));
  }
  Bytes third;
  const std::string mime = "application/epub+zip";
  local(third, "mimetype", Bytes(mime.begin(), mime.end()));
  local(third, "OEBPS/a.xhtml", pattern(100, 1));
  local(third, kSample, sample);
  EXPECT_EQ(hashOf(third), partialMd5(third));
}

TEST(KOReaderDocumentId, SampleClaimingMoreThanTheFileIsIgnored) {
  Bytes file;
  const std::string mime = "application/epub+zip";
  local(file, "mimetype", Bytes(mime.begin(), mime.end()));
  local(file, kSample, pattern(100, 2), 0, 0, 4000);
  EXPECT_EQ(hashOf(file), partialMd5(file));
}

TEST(KOReaderDocumentId, PlainFilesKeepThePartialMd5) {
  for (size_t n : {size_t{10}, size_t{1024}, size_t{1025}, size_t{70'000}, size_t{3'000'000}}) {
    const Bytes text = pattern(n, 5);
    EXPECT_EQ(hashOf(text), partialMd5(text)) << n;
  }
}

TEST(KOReaderDocumentId, AHostileFirstEntrySizeDoesNotWrap) {
  Bytes file;
  local(file, "mimetype", pattern(20, 1), 0, 0, 0xFFFFFFF0u);
  local(file, kSample, pattern(100, 2));
  EXPECT_EQ(hashOf(file), partialMd5(file));
}
