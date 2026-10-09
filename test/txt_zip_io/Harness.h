#pragma once
#include <algorithm>
#include <cstdint>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

inline size_t allocationLimit = std::numeric_limits<size_t>::max();
inline size_t largestAllocation = 0;
inline size_t rejectedAllocations = 0;
inline size_t arrayAllocationCalls = 0;
inline size_t failArrayAllocationAt = 0;
void* operator new(size_t size) {
  largestAllocation = std::max(largestAllocation, size);
  if (size > allocationLimit) { ++rejectedAllocations; throw std::bad_alloc(); }
  if (void* memory = std::malloc(size ? size : 1)) return memory;
  throw std::bad_alloc();
}
void* operator new[](size_t size) {
  ++arrayAllocationCalls;
  if (failArrayAllocationAt && arrayAllocationCalls == failArrayAllocationAt) throw std::bad_alloc();
  return ::operator new(size);
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  try { return ::operator new[](size); } catch (const std::bad_alloc&) { return nullptr; }
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
struct AllocationProbe {
  explicit AllocationProbe(size_t limit) { allocationLimit = limit; largestAllocation = rejectedAllocations = 0; }
  ~AllocationProbe() { allocationLimit = std::numeric_limits<size_t>::max(); }
};

#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_WARN(...) ((void)0)
inline void vTaskDelay(int) {}

struct FileData {
  std::vector<uint8_t> bytes;
  std::vector<int> readPlan;
  size_t readIndex = 0;
  size_t readCalls = 0;
  size_t writeCalls = 0;
  int failReadAtCall = -1;
  int writeLimit = -1;
  bool syncOk = true;
  bool closeOk = true;
  bool seekOk = true;
};

class HalFile {
 public:
  std::shared_ptr<FileData> data;
  size_t pos = 0;
  int read(void* buffer, size_t requested) {
    if (!data) return -1;
    ++data->readCalls;
    if (data->failReadAtCall > 0 && data->readCalls >= static_cast<size_t>(data->failReadAtCall)) return -1;
    int result = static_cast<int>(std::min(requested, data->bytes.size() - std::min(pos, data->bytes.size())));
    if (data->readIndex < data->readPlan.size()) result = data->readPlan[data->readIndex++];
    if (result <= 0) return result;
    if (static_cast<size_t>(result) > requested) throw std::runtime_error("Mock returned impossible read length");
    if (static_cast<size_t>(result) > data->bytes.size() - std::min(pos, data->bytes.size()))
      throw std::runtime_error("Mock read exceeds fixture");
    std::memcpy(buffer, data->bytes.data() + pos, result);
    pos += result;
    return result;
  }
  size_t write(const void* buffer, size_t requested) {
    if (!data) return 0;
    ++data->writeCalls;
    const size_t count = data->writeLimit < 0 ? requested : std::min(requested, static_cast<size_t>(data->writeLimit));
    if (pos + count > data->bytes.size()) data->bytes.resize(pos + count);
    if (count) std::memcpy(data->bytes.data() + pos, buffer, count);
    pos += count;
    return count;
  }
  bool seek(size_t offset) { if (!data || !data->seekOk || offset > data->bytes.size()) return false; pos = offset; return true; }
  bool seekCur(int offset) { return seek(pos + offset); }
  size_t size() const { return data ? data->bytes.size() : 0; }
  size_t position() const { return pos; }
  size_t available() const { return size() - std::min(pos, size()); }
  bool sync() { return data && data->syncOk; }
  void flush() {}
  bool close() { const bool ok = data && data->closeOk; data.reset(); return ok; }
  explicit operator bool() const { return data != nullptr; }
};

struct FakeStorage {
  std::map<std::string, std::shared_ptr<FileData>> files;
  int writeLimit = -1;
  bool syncOk = true;
  bool renameOk = true;
  int writeOpens = 0;
  int renames = 0;
  int removes = 0;
  std::shared_ptr<FileData> put(const std::string& path, const std::string& text) {
    auto file = std::make_shared<FileData>();
    file->bytes.assign(text.begin(), text.end()); files[path] = file; return file;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& out) {
    auto found = files.find(path); if (found == files.end()) return false;
    out.data = found->second; out.pos = 0; return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    ++writeOpens; out.data = put(path, ""); out.pos = 0;
    out.data->writeLimit = writeLimit; out.data->syncOk = syncOk; return true;
  }
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) { ++removes; return files.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    ++renames; if (!renameOk) return false;
    auto found = files.find(from); if (found == files.end()) return false;
    files[to] = found->second; files.erase(found); return true;
  }
} inline Storage;

class Txt {
 public:
  std::string filepath = "/book.txt";
  bool loaded = true;
  size_t fileSize = 0;
  size_t getFileSize() const { return fileSize; }
  std::string getCachePath() const { return "/cache"; }
  bool readContent(uint8_t*, size_t, size_t) const;
};

class Print {
 public:
  std::vector<uint8_t> bytes;
  size_t maxSpan = 0;
  size_t allowedSpan = 1024;
  size_t limit = std::numeric_limits<size_t>::max();
  size_t write(const uint8_t* data, size_t count) {
    maxSpan = std::max(maxSpan, count);
    if (count > allowedSpan) return 0;
    const auto take = std::min(count, limit);
    bytes.insert(bytes.end(), data, data + take);
    return take;
  }
};

constexpr uint16_t ZIP_METHOD_STORED = 0;
constexpr uint16_t ZIP_METHOD_DEFLATED = 8;
class ZipFile {
 public:
  struct FileStatSlim { uint16_t method = 0; uint32_t compressedSize = 0, uncompressedSize = 0, localHeaderOffset = 0; } stat;
  HalFile file;
  bool loadFileStatSlim(const char*, FileStatSlim* out) { *out = stat; return true; }
  long getDataOffset(const FileStatSlim&) { return 0; }
  bool readFileToStream(const char*, Print&, size_t, bool = false);
};
struct ScopedOpenClose { explicit ScopedOpenClose(ZipFile&) {} explicit operator bool() const { return true; } };
struct ZipInflateCtx { HalFile* file = nullptr; size_t fileRemaining = 0; uint8_t* readBuf = nullptr; size_t readBufSize = 0; };
inline size_t zipFillCallback(void*, const uint8_t**) { return 0; }
// Deflated parsing is covered by the production InflateStream suite. These cases
// exercise the stored-entry branch; retain its sibling branch for compilation.
class InflateStream {
 public:
  enum class Status { Done, Error, Ok };
  static inline bool enabled = false;
  static inline Status configuredStatus = Status::Error;
  static inline size_t configuredProduced = 0;
  bool init(bool) { return enabled; }
  void setFill(size_t (*)(void*, const uint8_t**), void*) {}
  Status readAtMost(uint8_t* output, size_t capacity, size_t* produced) {
    *produced = std::min(capacity, configuredProduced);
    std::memset(output, 'x', *produced);
    return configuredStatus;
  }
};

struct CrossPointSettings { enum { LEFT_ALIGN = 0, JUSTIFIED = 1 }; };
struct Settings {
  uint8_t readerInkWeight = 0, textAntiAliasing = 0;
  uint8_t screenMargin = 0, paragraphAlignment = 0, extraParagraphSpacing = 0, wordSpacing = 0, paragraphIndent = 0;
  int8_t letterSpacing = 0;
  int getReaderFontId() const { return 0; }
  int getReaderLineCompression() const { return 0; }
} inline SETTINGS;
namespace EpdFontFamily { constexpr int REGULAR = 0; }
namespace readerSpacing {
inline int paragraphGap(uint8_t, int) { return 0; }
inline int8_t letterPixels(int8_t value) { return value; }
inline int indentSpaces(uint8_t) { return 0; }
}
struct GfxRenderer {
  mutable int textScans = 0;
  int getScreenWidth() const { return 16; }
  int getScreenHeight() const { return 2; }
  int getLineHeight(int, int) const { return 1; }
  int getFontAscenderSize(int) const { return 1; }
  int getFontDescenderSize(int) const { return 0; }
  int getSpaceWidth(int, int, uint8_t) const { return 1; }
  bool isSdCardFont(int) const { return true; }
  void ensureSdCardFontReady(int, const char*, int) const { ++textScans; }
  int getTextAdvanceX(int, const char* text, int, int8_t, uint8_t) const { return std::strlen(text); }
};
struct Gui { void drawPopup(GfxRenderer&, const char*) {} } inline GUI;
constexpr int STR_INDEXING = 0;
inline const char* tr(int) { return "indexing"; }
// The indexing notice comes through the voice of the shell (readerugly::notice); here it is the plain string.
namespace StrId { constexpr int STR_INDEXING = ::STR_INDEXING; }
namespace readerugly { inline const char* notice(int id) { return tr(id); } }

inline size_t chunkAllocations = 0;
inline size_t failChunkAllocationAt = 0;
inline void* tracedMalloc(size_t size) {
  ++chunkAllocations;
  if (failChunkAllocationAt && chunkAllocations == failChunkAllocationAt) return nullptr;
  return std::malloc(size);
}
#define malloc tracedMalloc
