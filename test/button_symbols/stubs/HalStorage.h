#pragma once
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

// In-memory SD boundary. Font parsing, lookup, cache ownership and rendering
// use the complete production translation units.
struct HalFile {
  const std::vector<uint8_t>* bytes = nullptr;
  size_t offset_ = 0;
  inline static size_t shortReadAt = std::numeric_limits<size_t>::max();
  inline static size_t failSeekAt = std::numeric_limits<size_t>::max();
  inline static size_t readCalls = 0;
  size_t position() const { return offset_; }
  size_t size() const { return bytes ? bytes->size() : 0; }
  bool seek(size_t offset) { return seekSet(offset); }
  bool seekSet(size_t offset) {
    if (!bytes || offset == failSeekAt || offset > bytes->size()) return false;
    offset_ = offset;
    return true;
  }
  int read(void* out, size_t size) {
    if (!bytes) return -1;
    ++readCalls;
    size_t actual = std::min(size, bytes->size() - offset_);
    if (offset_ == shortReadAt && actual) --actual;
    std::memcpy(out, bytes->data() + offset_, actual);
    offset_ += actual;
    return static_cast<int>(actual);
  }
  int read() {
    uint8_t byte = 0;
    return read(&byte, 1) == 1 ? byte : -1;
  }
  bool seekCur(const int delta) { return seekSet(offset_ + delta); }
  explicit operator bool() const { return bytes != nullptr; }
  size_t write(const uint8_t*, size_t size) { return size; }
  void close() { bytes = nullptr; }
};
struct HostStorage {
  std::map<std::string, std::vector<uint8_t>> files;
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    const auto it = files.find(path);
    if (it == files.end()) return false;
    file.bytes = &it->second;
    file.offset_ = 0;
    return true;
  }
};
inline HostStorage Storage;
