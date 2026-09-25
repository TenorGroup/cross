#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
namespace storageMetrics {
inline bool enabled = false;
inline uint64_t readCalls = 0, readBytes = 0, writeCalls = 0, writeBytes = 0, seekCalls = 0, sidecarMaxBytes = 0;
inline uint64_t htmlReadCalls = 0, htmlReadBytes = 0, htmlWriteCalls = 0, htmlWriteBytes = 0, htmlSeekCalls = 0;
inline uint64_t cacheReadCalls = 0, cacheReadBytes = 0, cacheWriteCalls = 0, cacheWriteBytes = 0,
                cacheSeekCalls = 0;
inline uint64_t renameCalls = 0;
// Writes to the page index staging file (<section>.lut.part) while a build runs.
inline uint64_t lutWriteCalls = 0;
inline std::string prefixSourcePath, prefixStagingPath;
inline size_t prefixBegin = 0, prefixEnd = 0;
inline uint64_t prefixReadCalls = 0, prefixReadBytes = 0, prefixWriteCalls = 0, prefixWriteBytes = 0;
inline bool isHtml(const std::string& path) { return path.find("/html/") != std::string::npos; }
inline bool isCache(const std::string& path) { return path.find("/sections/") != std::string::npos; }
inline void begin() {
  readCalls = readBytes = writeCalls = writeBytes = seekCalls = sidecarMaxBytes = 0;
  htmlReadCalls = htmlReadBytes = htmlWriteCalls = htmlWriteBytes = htmlSeekCalls = 0;
  cacheReadCalls = cacheReadBytes = cacheWriteCalls = cacheWriteBytes = cacheSeekCalls = 0;
  renameCalls = lutWriteCalls = 0;
  prefixReadCalls = prefixReadBytes = prefixWriteCalls = prefixWriteBytes = 0;
  prefixSourcePath.clear(); prefixStagingPath.clear(); prefixBegin = prefixEnd = 0;
  enabled = true;
}
inline void watchPrefix(const std::string& source, size_t begin, size_t end) {
  prefixSourcePath = source;
  prefixStagingPath = source + ".part";
  prefixBegin = begin;
  prefixEnd = end;
}
inline void recordRead(const std::string& path, size_t at, size_t count) {
  ++readCalls;
  readBytes += count;
  if (isHtml(path)) {
    ++htmlReadCalls;
    htmlReadBytes += count;
  } else if (isCache(path)) {
    ++cacheReadCalls;
    cacheReadBytes += count;
  }
  if (path == prefixSourcePath && at >= prefixBegin && at < prefixEnd) {
    ++prefixReadCalls;
    prefixReadBytes += count;
  }
}
inline void recordWrite(const std::string& path, size_t at, size_t count) {
  ++writeCalls;
  writeBytes += count;
  if (path.ends_with(".lut.part")) ++lutWriteCalls;
  if (isHtml(path)) {
    ++htmlWriteCalls;
    htmlWriteBytes += count;
  } else if (isCache(path)) {
    ++cacheWriteCalls;
    cacheWriteBytes += count;
  }
  if (path == prefixStagingPath && at >= prefixBegin && at < prefixEnd) {
    ++prefixWriteCalls;
    prefixWriteBytes += count;
  }
}
inline void recordSeek(const std::string& path) {
  ++seekCalls;
  if (isHtml(path)) {
    ++htmlSeekCalls;
  } else if (isCache(path)) {
    ++cacheSeekCalls;
  }
}
}
namespace storageLutWrites {
struct Event { size_t offset, bytes; };
inline bool enabled = false;
inline std::vector<Event> events;
inline void begin() { events.clear(); enabled = true; }
inline void record(const std::string& path, size_t offset, size_t bytes) {
  if (enabled && path.ends_with("/sections/0.bin.part")) events.push_back({offset, bytes});
}
}
namespace storageFault {
inline std::string target;
inline size_t writeOffset = std::numeric_limits<size_t>::max();
inline size_t readOffset = std::numeric_limits<size_t>::max();
inline size_t writeEndOffset = std::numeric_limits<size_t>::max();
inline size_t readEndOffset = std::numeric_limits<size_t>::max();
inline bool failOnce = false;
inline bool shortReadOnce = false;
inline bool failFlush = false;
inline bool failRename = false;
inline bool failRemove = false;
inline bool failSeek = false;
inline bool failClose = false;
inline unsigned hit = 0;
inline size_t hitOffset = std::numeric_limits<size_t>::max();
inline void reset() {
  target.clear(); writeOffset = readOffset = std::numeric_limits<size_t>::max();
  writeEndOffset = readEndOffset = hitOffset = std::numeric_limits<size_t>::max();
  failOnce = shortReadOnce = failFlush = failRename = failRemove = failSeek = failClose = false; hit = 0;
}
inline bool selected(const std::string& path) { return !target.empty() && path.ends_with(target); }
}
class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  bool open(const char* path, const char* mode) { close(); path_ = path; file_ = std::fopen(path, mode); return file_; }
  int available() const { return file_ ? static_cast<int>(size() - position()) : 0; }
  int read(void* buffer, size_t count) {
    const size_t at = position();
    const bool shortRead = storageFault::shortReadOnce && storageFault::selected(path_) &&
                           at >= storageFault::readOffset && at < storageFault::readEndOffset;
    if (shortRead) { storageFault::shortReadOnce = false; ++storageFault::hit; storageFault::hitOffset = at; }
    const int actual = file_ ? static_cast<int>(std::fread(buffer, 1, shortRead && count ? count - 1 : count, file_)) : -1;
    if (storageMetrics::enabled) storageMetrics::recordRead(path_, at, actual > 0 ? static_cast<size_t>(actual) : 0);
    return actual;
  }
  size_t write(const void* buffer, size_t count) {
    if (!file_) return 0;
    const size_t at = position();
    if (storageFault::failOnce && storageFault::selected(path_) &&
        at >= storageFault::writeOffset && at < storageFault::writeEndOffset) {
      storageFault::failOnce = false; ++storageFault::hit; storageFault::hitOffset = at;
      const size_t actual = count ? std::fwrite(buffer, 1, count - 1, file_) : 0;
      if (storageMetrics::enabled) storageMetrics::recordWrite(path_, at, actual);
      storageLutWrites::record(path_, at, actual);
      return actual;
    }
    const size_t actual = std::fwrite(buffer, 1, count, file_);
    if (storageMetrics::enabled) {
      storageMetrics::recordWrite(path_, at, actual);
      if (path_.ends_with(".lut.part")) storageMetrics::sidecarMaxBytes = std::max(storageMetrics::sidecarMaxBytes, uint64_t(at + actual));
    }
    storageLutWrites::record(path_, at, actual);
    return actual;
  }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  bool flush() { if (storageFault::failFlush && storageFault::selected(path_)) { ++storageFault::hit; return false; } return file_ && std::fflush(file_) == 0; }
  bool sync() { return flush(); }
  bool seek(size_t offset) { if (storageMetrics::enabled) storageMetrics::recordSeek(path_); if (storageFault::failSeek && storageFault::selected(path_)) { ++storageFault::hit; return false; } return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_SET) == 0; }
  bool seekCur(size_t offset) { return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_CUR) == 0; }
  bool close() {
    if (!file_) return false;
    const bool injected = storageFault::failClose && storageFault::selected(path_);
    if (injected) { storageFault::failClose = false; ++storageFault::hit; }
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    return ok && !injected;
  }
  bool isOpen() const { return file_ != nullptr; }
  explicit operator bool() const { return isOpen(); }
  size_t position() const { return file_ ? static_cast<size_t>(std::ftell(file_)) : 0; }
  size_t size() const { if (!file_) return 0; const long p=std::ftell(file_); std::fseek(file_,0,SEEK_END); const long e=std::ftell(file_); std::fseek(file_,p,SEEK_SET); return e > 0 ? e : 0; }
 private:
  std::FILE* file_ = nullptr;
  std::string path_;
};
class HalStorage {
 public:
  static HalStorage& getInstance() { static HalStorage s; return s; }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "rb"); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "w+b"); }
  bool exists(const char* path) const { return std::filesystem::exists(path); }
  bool remove(const std::string& path) {
    if (storageFault::failRemove && storageFault::selected(path)) { ++storageFault::hit; return false; }
    return std::remove(path.c_str()) == 0;
  }
  bool mkdir(const char* path) { std::error_code e; std::filesystem::create_directories(path,e); return !e; }
  bool rename(const char* from, const char* to) {
    if (storageMetrics::enabled) ++storageMetrics::renameCalls;
    if (storageFault::failRename && storageFault::selected(from)) { ++storageFault::hit; return false; }
    return std::rename(from,to) == 0;
  }
};
#define Storage HalStorage::getInstance()
inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
