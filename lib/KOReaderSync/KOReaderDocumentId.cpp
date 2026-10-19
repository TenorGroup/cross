#include "KOReaderDocumentId.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>

#include <algorithm>
#include <cstring>

namespace {
// Extract filename from path (everything after last '/')
std::string getFilename(const std::string& path) {
  const size_t pos = path.rfind('/');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

// The web page's "keep KOReader sync" optimisation stores the partial-MD5 windows of the original file as
// the entry right after `mimetype`, uncompressed. Hashing that entry gives the original's id, so KOReader
// holding the original and this device holding the smaller copy share one record.
constexpr char kSampleName[] = "META-INF/tenor-koreader-sample.bin";
constexpr uint32_t kZipLocal = 0x04034b50;
constexpr size_t kMaxSample = 12 * 1024;

uint16_t le16(const uint8_t* b) { return b[0] | (b[1] << 8); }
uint32_t le32(const uint8_t* b) { return le16(b) | (static_cast<uint32_t>(le16(b + 2)) << 16); }

bool hashEmbeddedSample(HalFile& file, const size_t fileSize, MD5Builder& md5, uint8_t* buffer, const size_t capacity) {
  uint8_t header[30];
  if (!file.seekSet(0) || file.read(header, 30) != 30 || le32(header) != kZipLocal) return false;
  // 64-bit sums: size_t is 32 bits on the device and a hostile header must not wrap around.
  const uint64_t second = 30ull + le16(header + 26) + le16(header + 28) + le32(header + 18);
  if (second + 30 > fileSize || !file.seekSet(static_cast<size_t>(second)) || file.read(header, 30) != 30) return false;
  const size_t nameLength = le16(header + 26), size = le32(header + 18);
  const uint64_t body = second + 30 + nameLength + le16(header + 28);
  char name[sizeof(kSampleName)];
  if (le32(header) != kZipLocal || (le16(header + 6) & ~0x0800u) || le16(header + 8) != 0 || size != le32(header + 22) ||
      size == 0 || size > kMaxSample || nameLength != sizeof(kSampleName) - 1 || body + size > fileSize ||
      static_cast<size_t>(file.read(name, nameLength)) != nameLength ||
      std::memcmp(name, kSampleName, nameLength) != 0 || !file.seekSet(static_cast<size_t>(body)))
    return false;
  md5.begin();
  for (size_t left = size; left;) {
    const size_t count = std::min(left, capacity);
    if (static_cast<size_t>(file.read(buffer, count)) != count) return false;
    md5.add(buffer, count);
    left -= count;
  }
  return true;
}
}  // namespace

std::string KOReaderDocumentId::calculateFromFilename(const std::string& filePath) {
  const std::string filename = getFilename(filePath);
  if (filename.empty()) {
    return "";
  }

  MD5Builder md5;
  md5.begin();
  md5.add(filename.c_str());
  md5.calculate();

  std::string result = md5.toString().c_str();
  LOG_DBG("KODoc", "Filename hash: %s (from '%s')", result.c_str(), filename.c_str());
  return result;
}

size_t KOReaderDocumentId::getOffset(int i) {
  // Offset = 1024 << (2*i)
  // For i = -1: KOReader uses a value of 0
  // For i >= 0: 1024 << (2*i)
  if (i < 0) {
    return 0;
  }
  return CHUNK_SIZE << (2 * i);
}

std::string KOReaderDocumentId::calculate(const std::string& filePath) {
  HalFile file;
  if (!Storage.openFileForRead("KODoc", filePath, file)) {
    LOG_DBG("KODoc", "Failed to open file: %s", filePath.c_str());
    return "";
  }

  const size_t fileSize = file.fileSize();
  LOG_DBG("KODoc", "Calculating hash for file: %s (size: %zu)", filePath.c_str(), fileSize);

  // Initialize MD5 builder
  MD5Builder md5;

  // Buffer for reading chunks
  uint8_t buffer[CHUNK_SIZE];
  if (hashEmbeddedSample(file, fileSize, md5, buffer, sizeof(buffer))) {
    md5.calculate();
    std::string result = md5.toString().c_str();
    LOG_DBG("KODoc", "Hash of the original file kept in the book: %s", result.c_str());
    return result;
  }
  md5.begin();
  size_t totalBytesRead = 0;

  // Read from each offset (i = -1 to 10)
  for (int i = -1; i < OFFSET_COUNT - 1; i++) {
    const size_t offset = getOffset(i);

    // Skip if offset is beyond file size
    if (offset >= fileSize) {
      continue;
    }

    // Seek to offset
    if (!file.seekSet(offset)) {
      LOG_DBG("KODoc", "Failed to seek to offset %zu", offset);
      continue;
    }

    // Read up to CHUNK_SIZE bytes
    const size_t bytesToRead = std::min(CHUNK_SIZE, fileSize - offset);
    const size_t bytesRead = file.read(buffer, bytesToRead);

    if (bytesRead > 0) {
      md5.add(buffer, bytesRead);
      totalBytesRead += bytesRead;
    }
  }

  // Calculate final hash
  md5.calculate();
  std::string result = md5.toString().c_str();

  LOG_DBG("KODoc", "Hash calculated: %s (from %zu bytes)", result.c_str(), totalBytesRead);

  return result;
}
