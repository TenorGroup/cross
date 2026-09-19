#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
namespace storageMetrics {
inline bool enabled = false;
inline uint64_t readCalls = 0, readBytes = 0, writeCalls = 0, writeBytes = 0, seekCalls = 0, sidecarMaxBytes = 0;
inline void begin() { readCalls = readBytes = writeCalls = writeBytes = seekCalls = sidecarMaxBytes = 0; enabled = true; }
}
namespace storageFault {
inline std::string target;
inline size_t writeOffset = std::numeric_limits<size_t>::max();
inline bool failOnce = false;
inline bool failFlush = false;
inline bool failRename = false;
inline bool failRemove = false;
inline bool failSeek = false;
inline unsigned hit = 0;
inline void reset() {
  target.clear(); writeOffset = std::numeric_limits<size_t>::max();
  failOnce = failFlush = failRename = failRemove = failSeek = false; hit = 0;
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
    const int actual = file_ ? static_cast<int>(std::fread(buffer, 1, count, file_)) : -1;
    if (storageMetrics::enabled) { ++storageMetrics::readCalls; if (actual > 0) storageMetrics::readBytes += actual; }
    return actual;
  }
  size_t write(const void* buffer, size_t count) {
    if (!file_) return 0;
    if (storageFault::failOnce && storageFault::selected(path_) && position() >= storageFault::writeOffset) {
      storageFault::failOnce = false; ++storageFault::hit;
      return count ? std::fwrite(buffer, 1, count - 1, file_) : 0;
    }
    const size_t at = position();
    const size_t actual = std::fwrite(buffer, 1, count, file_);
    if (storageMetrics::enabled) {
      ++storageMetrics::writeCalls; storageMetrics::writeBytes += actual;
      if (path_.ends_with(".lut.part")) storageMetrics::sidecarMaxBytes = std::max(storageMetrics::sidecarMaxBytes, uint64_t(at + actual));
    }
    return actual;
  }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  bool flush() { if (storageFault::failFlush && storageFault::selected(path_)) { ++storageFault::hit; return false; } return file_ && std::fflush(file_) == 0; }
  bool sync() { return flush(); }
  bool seek(size_t offset) { if (storageMetrics::enabled) ++storageMetrics::seekCalls; if (storageFault::failSeek && storageFault::selected(path_)) { ++storageFault::hit; return false; } return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_SET) == 0; }
  bool seekCur(size_t offset) { return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_CUR) == 0; }
  bool close() { if (!file_) return false; const bool ok = std::fclose(file_) == 0; file_ = nullptr; return ok; }
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
  bool rename(const char* from, const char* to) { if (storageFault::failRename && storageFault::selected(from)) { ++storageFault::hit; return false; } return std::rename(from,to) == 0; }
};
#define Storage HalStorage::getInstance()
inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
