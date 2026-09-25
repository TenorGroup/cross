#include "ZipFile.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>

struct ZipInflateCtx {
  HalFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};

namespace {
constexpr uint16_t ZIP_METHOD_STORED = 0;
constexpr uint16_t ZIP_METHOD_DEFLATED = 8;

// RAII zip: opens the zip if not already open, closes on destruction only if
// it performed the open.  Removes the wasOpen/close boilerplate from every method.
class ScopedOpenClose final {
 public:
  [[nodiscard]] explicit ScopedOpenClose(ZipFile& zf) : zf(zf), needsClose(!zf.isOpen()) {
    if (needsClose) ok = zf.open();
  }
  ~ScopedOpenClose() {
    if (needsClose && ok) zf.close();
  }
  ScopedOpenClose(const ScopedOpenClose&) = delete;
  ScopedOpenClose& operator=(const ScopedOpenClose&) = delete;
  ScopedOpenClose(ScopedOpenClose&&) = delete;
  ScopedOpenClose& operator=(ScopedOpenClose&&) = delete;
  explicit operator bool() const { return ok || !needsClose; }

 private:
  ZipFile& zf;
  bool needsClose = false;
  bool ok = true;  // true when zip was already open (no open() call needed)
};

size_t zipFillCallback(void* vctx, const uint8_t** data) {
  auto* ctx = static_cast<ZipInflateCtx*>(vctx);
  if (ctx->fileRemaining == 0) return 0;

  const size_t toRead = ctx->fileRemaining < ctx->readBufSize ? ctx->fileRemaining : ctx->readBufSize;
  const int result = ctx->file->read(ctx->readBuf, toRead);
  // HalFile::read() returns a negative int on error. Treat it as end-of-stream
  // rather than letting the negative-to-size_t conversion underflow fileRemaining
  // and report a huge bytesRead, which would have the inflate library read past
  // the end of readBuf.
  if (result <= 0 || static_cast<size_t>(result) > toRead) {
    LOG_ERR("ZIP", "Failed to read compressed data: %d", result);
    return 0;
  }
  const size_t bytesRead = static_cast<size_t>(result);
  ctx->fileRemaining -= bytesRead;

  *data = ctx->readBuf;
  return bytesRead;
}

// Every walk of the central directory logs its entries and time (CD_SCAN): at info level on the
// measurement builds, at debug level elsewhere.
#if defined(TENOR_UI_ACCEPTANCE) || defined(TENOR_PRESS_PROBE)
#define ZIP_CD_LOG LOG_INF
#else
#define ZIP_CD_LOG LOG_DBG
#endif

constexpr uint32_t CENTRAL_SIGNATURE = 0x02014b50;
constexpr size_t CENTRAL_HEADER = 46;
constexpr size_t MAX_NAME = 255;
// The walk reads the directory in blocks: a book of 5.000 chapters has some 375 KB of it, and one
// field at a time took about 70.000 calls under the card's lock (2,3 s on the X3 for the cover at
// its end). 4 KB while the heap has eight times that; the smallest block that holds one whole
// entry head and name comes from the stack.
constexpr size_t WALK_BLOCK = 4096;
constexpr size_t WALK_BLOCK_SMALL = 1024;
constexpr size_t WALK_BLOCK_MIN = CENTRAL_HEADER + MAX_NAME;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return le16(p) | static_cast<uint32_t>(le16(p + 2)) << 16; }

// A window of the zip in RAM: [start, start + len).
class CentralReader {
 public:
  CentralReader(HalFile& file, uint8_t* buf, const size_t cap, const uint32_t end)
      : file(file), buf(buf), cap(cap), end(end) {}
  // Brings [at, at + need) into the window, reading a whole block from `at` when it is not there.
  bool have(const uint32_t at, const size_t need) {
    if (at >= start && at - start + need <= len) return true;
    if (need > cap || at > end || need > end - at) return false;
    const size_t want = std::min<size_t>(cap, end - at);
    len = 0;
    if (!file.seek(at)) return false;
    const int got = file.read(buf, want);
    reads++;
    if (got < static_cast<int>(need)) return false;
    start = at;
    len = static_cast<size_t>(got);
    return true;
  }
  const uint8_t* at(const uint32_t pos) const { return buf + (pos - start); }
  unsigned reads = 0;

 private:
  HalFile& file;
  uint8_t* buf;
  size_t cap;
  uint32_t end;
  uint32_t start = 0;
  size_t len = 0;
};

// Entries the zips answered lately, across ZipFile objects: the reader asks an item's size and then
// its bytes from two new ZipFiles, and reads chapters in the order they sit in the directory. A slot
// holds for the zip as it was (ZipFile::zipKey); a book replaced on the card misses and is walked.
struct Known {
  uint64_t zip = 0, name = 0;
  uint16_t nameLen = 0;
  uint32_t next = 0;
  ZipFile::FileStatSlim stat = {};
};
constexpr size_t KNOWN_SLOTS = 8;
Known known[KNOWN_SLOTS];
size_t knownNext = 0;
std::mutex knownLock;
}  // namespace

uint64_t ZipFile::zipKey() const {
  uint8_t details[10];
  const uint32_t size = static_cast<uint32_t>(const_cast<HalFile&>(file).size());
  std::memcpy(details, &size, 4);
  std::memcpy(details + 4, &zipDetails.centralDirOffset, 4);
  std::memcpy(details + 8, &zipDetails.totalEntries, 2);
  return fnvHash64(filePath.data(), filePath.size()) ^ fnvHash64(reinterpret_cast<const char*>(details), 10);
}

bool ZipFile::walkCentralDir(uint32_t from, const bool wrap, [[maybe_unused]] const char* kind, const Visit visit,
                             void* ctx) {
  const ScopedOpenClose zip{*this};
  if (!zip || !loadZipDetails()) return false;
  [[maybe_unused]] const unsigned long started = millis();
  const uint32_t begin = zipDetails.centralDirOffset;
  const uint32_t end = static_cast<uint32_t>(file.size());
  if (from < begin) from = begin;
  const size_t cap = ESP.getFreeHeap() >= 8 * WALK_BLOCK        ? WALK_BLOCK
                     : ESP.getFreeHeap() >= 8 * WALK_BLOCK_SMALL ? WALK_BLOCK_SMALL
                                                                 : 0;
  const auto block = cap ? std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[cap]) : nullptr;
  uint8_t small[WALK_BLOCK_MIN];
  CentralReader reader(file, block ? block.get() : small, block ? cap : sizeof(small), end);

  uint32_t pos = from;
  bool wrapped = false, stopped = false;
  [[maybe_unused]] unsigned entries = 0;
  while (!(wrapped && pos >= from)) {
    if (!reader.have(pos, CENTRAL_HEADER) || le32(reader.at(pos)) != CENTRAL_SIGNATURE) {
      // The end of the directory (or of what is readable of it): once more from its start.
      if (wrap && !wrapped && from != begin) {
        wrapped = true;
        pos = begin;
        continue;
      }
      break;
    }
    const uint8_t* head = reader.at(pos);
    CentralEntry entry;
    entry.method = le16(head + 10);
    entry.crc32 = le32(head + 16);
    entry.compressedSize = le32(head + 20);
    entry.uncompressedSize = le32(head + 24);
    entry.nameLen = le16(head + 28);
    const uint32_t extra = le16(head + 30) + static_cast<uint32_t>(le16(head + 32));
    entry.localHeaderOffset = le32(head + 42);
    entry.next = pos + CENTRAL_HEADER + entry.nameLen + extra;
    entry.name = nullptr;
    if (entry.nameLen <= MAX_NAME) {
      // A name cut off by the end of the file ends the walk.
      if (!reader.have(pos, CENTRAL_HEADER + entry.nameLen)) break;
      entry.name = reinterpret_cast<const char*>(reader.at(pos) + CENTRAL_HEADER);
    }
    entries++;
    if (visit(ctx, entry)) {
      stopped = true;
      break;
    }
    pos = entry.next;
  }
  ZIP_CD_LOG("ZIP", "CD_SCAN kind=%s entries=%u ms=%lu reads=%u block=%u found=%u", kind, entries,
             millis() - started, reader.reads, static_cast<unsigned>(block ? cap : sizeof(small)), stopped ? 1u : 0u);
  return true;
}

bool ZipFile::loadAllFileStatSlims() {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  fileStatSlimCache.clear();
  fileStatSlimCache.reserve(zipDetails.totalEntries);
  walkCentralDir(0, false, "all",
                 [](void* ctx, const CentralEntry& entry) {
                   if (entry.name)
                     static_cast<ZipFile*>(ctx)->fileStatSlimCache.emplace(
                         std::string(entry.name, entry.nameLen),
                         FileStatSlim{entry.method, entry.compressedSize, entry.uncompressedSize,
                                      entry.localHeaderOffset});
                   return false;
                 },
                 this);

  // Set cursor to start of central directory for sequential access
  lastCentralDirPos = zipDetails.centralDirOffset;
  lastCentralDirPosValid = true;

  return true;
}

bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* fileStat) {
  if (!fileStatSlimCache.empty()) {
    const auto it = fileStatSlimCache.find(filename);
    if (it != fileStatSlimCache.end()) {
      *fileStat = it->second;
      return true;
    }
    return false;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  const size_t nameLen = strlen(filename);
  const uint64_t key = zipKey();
  const uint64_t name = fnvHash64(filename, nameLen);
  // Where this object's last lookup ended, or else where the last one of this zip did.
  uint32_t from = lastCentralDirPosValid ? lastCentralDirPos : 0;
  {
    const std::lock_guard<std::mutex> lock(knownLock);
    for (size_t i = 0; i < KNOWN_SLOTS; i++) {
      const Known& slot = known[(knownNext + KNOWN_SLOTS - 1 - i) % KNOWN_SLOTS];
      if (slot.zip != key) continue;
      if (slot.name == name && slot.nameLen == nameLen) {
        *fileStat = slot.stat;
        lastCentralDirPos = slot.next;
        lastCentralDirPosValid = true;
        ZIP_CD_LOG("ZIP", "CD_SCAN kind=known entries=0 ms=0 reads=0 block=0 found=1");
        return true;
      }
      if (from == 0) from = slot.next;
    }
  }

  struct Search {
    const char* name;
    size_t nameLen;
    FileStatSlim* stat;
    uint32_t next;
  } search{filename, nameLen, fileStat, 0};
  walkCentralDir(from, true, "find",
                 [](void* ctx, const CentralEntry& entry) {
                   auto* s = static_cast<Search*>(ctx);
                   if (!entry.name || entry.nameLen != s->nameLen || memcmp(entry.name, s->name, s->nameLen) != 0)
                     return false;
                   *s->stat = {entry.method, entry.compressedSize, entry.uncompressedSize, entry.localHeaderOffset};
                   s->next = entry.next;
                   return true;
                 },
                 &search);
  if (search.next == 0) return false;
  lastCentralDirPos = search.next;
  lastCentralDirPosValid = true;
  const std::lock_guard<std::mutex> lock(knownLock);
  known[knownNext] = {key, name, static_cast<uint16_t>(nameLen), search.next, *fileStat};
  knownNext = (knownNext + 1) % KNOWN_SLOTS;
  return true;
}

long ZipFile::getDataOffset(const FileStatSlim& fileStat) {
  const ScopedOpenClose zip{*this};
  if (!zip) return -1;

  constexpr auto localHeaderSize = 30;

  uint8_t pLocalHeader[localHeaderSize];
  const uint64_t fileOffset = fileStat.localHeaderOffset;

  file.seek(fileOffset);
  const size_t read = file.read(pLocalHeader, localHeaderSize);

  if (read != localHeaderSize) {
    LOG_ERR("ZIP", "Something went wrong reading the local header");
    return -1;
  }

  if (pLocalHeader[0] + (pLocalHeader[1] << 8) + (pLocalHeader[2] << 16) + (pLocalHeader[3] << 24) !=
      0x04034b50 /* ZIP local file header signature */) {
    LOG_ERR("ZIP", "Not a valid zip file header");
    return -1;
  }

  const uint16_t filenameLength = pLocalHeader[26] + (pLocalHeader[27] << 8);
  const uint16_t extraOffset = pLocalHeader[28] + (pLocalHeader[29] << 8);
  return fileOffset + localHeaderSize + filenameLength + extraOffset;
}

bool ZipFile::loadZipDetails() {
  if (zipDetails.isSet) {
    return true;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  const size_t fileSize = file.size();
  if (fileSize < 22) {
    LOG_ERR("ZIP", "File too small to be a valid zip");
    return false;  // Minimum EOCD size is 22 bytes
  }

  // We scan the last 1KB (or the whole file if smaller) for the EOCD signature
  // 0x06054b50 is stored as 0x50, 0x4b, 0x05, 0x06 in little-endian
  const int scanRange = fileSize > 1024 ? 1024 : fileSize;
  const auto buffer = static_cast<uint8_t*>(malloc(scanRange));
  if (!buffer) {
    LOG_ERR("ZIP", "Failed to allocate memory for EOCD scan buffer");
    return false;
  }

  file.seek(fileSize - scanRange);
  file.read(buffer, scanRange);

  // Scan backwards for the signature
  int foundOffset = -1;
  for (int i = scanRange - 22; i >= 0; i--) {
    constexpr uint32_t signature = 0x06054b50;
    if (*reinterpret_cast<uint32_t*>(&buffer[i]) == signature) {
      foundOffset = i;
      break;
    }
  }

  if (foundOffset == -1) {
    LOG_ERR("ZIP", "EOCD signature not found in zip file");
    free(buffer);
    return false;
  }

  // Now extract the values we need from the EOCD record
  // Relative positions within EOCD:
  // Offset 10: Total number of entries (2 bytes)
  // Offset 16: Offset of start of central directory with respect to the starting disk number (4 bytes)
  zipDetails.totalEntries = *reinterpret_cast<uint16_t*>(&buffer[foundOffset + 10]);
  zipDetails.centralDirOffset = *reinterpret_cast<uint32_t*>(&buffer[foundOffset + 16]);
  zipDetails.isSet = true;

  free(buffer);
  return true;
}

bool ZipFile::open() {
  if (!Storage.openFileForRead("ZIP", filePath, file)) {
    return false;
  }
  return true;
}

bool ZipFile::close() {
  if (file) {
    // Explicit close() required: member variable persists beyond function scope
    file.close();
  }
  lastCentralDirPos = 0;
  lastCentralDirPosValid = false;
  return true;
}

bool ZipFile::getInflatedFileSize(const char* filename, size_t* size) {
  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  *size = static_cast<size_t>(fileStat.uncompressedSize);
  return true;
}

int ZipFile::fillUncompressedSizes(std::deque<SizeTarget>& targets, std::deque<uint32_t>& sizes) {
  if (targets.empty()) {
    return 0;
  }

  struct Fill {
    std::deque<SizeTarget>& targets;
    std::deque<uint32_t>& sizes;
    int matched;
  } fill{targets, sizes, 0};
  walkCentralDir(0, false, "sizes",
                 [](void* ctx, const CentralEntry& entry) {
                   auto* f = static_cast<Fill*>(ctx);
                   if (!entry.name) return false;
                   const uint64_t hash = fnvHash64(entry.name, entry.nameLen);
                   const SizeTarget key = {hash, entry.nameLen, 0};
                   auto it = std::lower_bound(f->targets.begin(), f->targets.end(), key,
                                              [](const SizeTarget& a, const SizeTarget& b) {
                                                return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
                                              });
                   while (it != f->targets.end() && it->hash == hash && it->len == entry.nameLen) {
                     if (it->index < f->sizes.size()) {
                       f->sizes[it->index] = entry.uncompressedSize;
                       f->matched++;
                     }
                     ++it;
                   }
                   return f->matched >= static_cast<int>(f->targets.size());
                 },
                 &fill);
  return fill.matched;
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, const bool trailingNullByte) {
  const ScopedOpenClose zip{*this};
  if (!zip) return nullptr;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return nullptr;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return nullptr;

  file.seek(fileOffset);

  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  const auto dataSize = trailingNullByte ? inflatedDataSize + 1 : inflatedDataSize;
  const auto data = static_cast<uint8_t*>(malloc(dataSize));
  if (data == nullptr) {
    LOG_ERR("ZIP", "Failed to allocate memory for output buffer (%zu bytes)", dataSize);
    return nullptr;
  }

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const size_t dataRead = file.read(data, inflatedDataSize);

    if (dataRead != inflatedDataSize) {
      LOG_ERR("ZIP", "Failed to read data");
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(1024));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      free(data);
      return nullptr;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = 1024;

    // One-shot mode: `data` holds the entire output, so back-references
    // resolve inside it and no 32KB window is allocated.
    InflateStream inflate;
    if (!inflate.init(false)) {
      LOG_ERR("ZIP", "Failed to init inflate stream for %s", filename);
      free(fileReadBuffer);
      free(data);
      return nullptr;
    }
    inflate.setFill(zipFillCallback, &ctx);

    if (!inflate.read(data, inflatedDataSize)) {
      LOG_ERR("ZIP", "Failed to inflate file");
      free(fileReadBuffer);
      free(data);
      return nullptr;
    }
    free(fileReadBuffer);

    // Continue out of block with data set
  } else {
    LOG_ERR("ZIP", "Unsupported compression method");
    free(data);
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize, const bool allowEarlyStop) {
  if (chunkSize == 0) return false;
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return false;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return false;

  if (!file.seek(fileOffset)) return false;
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const auto buffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!buffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for buffer");
      return false;
    }

    size_t remaining = inflatedDataSize;
    while (remaining > 0) {
      const size_t requested = std::min(remaining, chunkSize);
      const int result = file.read(buffer, requested);
      if (result <= 0 || static_cast<size_t>(result) > requested) {
        LOG_ERR("ZIP", "Could not read more bytes");
        free(buffer);
        return false;
      }

      const size_t dataRead = static_cast<size_t>(result);
      if (out.write(buffer, dataRead) != dataRead) {
        free(buffer);
        if (allowEarlyStop) return true;  // sink has what it needs
        LOG_ERR("ZIP", "Failed to write all output bytes to stream");
        return false;
      }
      remaining -= dataRead;
    }

    free(buffer);
    return true;
  }

  if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      return false;
    }

    auto* outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!outputBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for output buffer");
      free(fileReadBuffer);
      return false;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = chunkSize;

    InflateStream inflate;
    if (!inflate.init(true)) {
      LOG_ERR("ZIP", "Failed to init inflate stream for %s", filename);
      free(outputBuffer);
      free(fileReadBuffer);
      return false;
    }
    inflate.setFill(zipFillCallback, &ctx);

    bool success = false;
    size_t totalProduced = 0;

    while (true) {
      size_t produced;
      const InflateStream::Status status = inflate.readAtMost(outputBuffer, chunkSize, &produced);

      if (status == InflateStream::Status::Error) {
        LOG_ERR("ZIP", "Decompression failed");
        break;
      }

      totalProduced += produced;
      if (totalProduced > static_cast<size_t>(inflatedDataSize)) {
        LOG_ERR("ZIP", "Decompressed size exceeds expected (%zu > %zu)", totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }

      if (produced > 0) {
        if (out.write(outputBuffer, produced) != produced) {
          if (allowEarlyStop) {
            success = true;  // sink has what it needs
          } else {
            LOG_ERR("ZIP", "Failed to write all output bytes to stream");
          }
          break;
        }
      }

      if (status == InflateStream::Status::Done) {
        if (totalProduced != static_cast<size_t>(inflatedDataSize)) {
          LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(inflatedDataSize),
                  totalProduced);
          break;
        }
        LOG_DBG("ZIP", "Decompressed %d bytes into %d bytes", deflatedDataSize, inflatedDataSize);
        success = true;
        break;
      }

      // InflateStream::Status::Ok: output buffer full, continue
    }

    free(outputBuffer);
    free(fileReadBuffer);
    return success;  // inflate destructor frees the decompressor state + window
  }

  LOG_ERR("ZIP", "Unsupported compression method");
  return false;
}
