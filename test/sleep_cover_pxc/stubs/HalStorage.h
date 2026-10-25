#pragma once
#include <Arduino.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Files in RAM: what the test writes, and what the code under test reads and writes.
inline std::map<std::string, std::vector<uint8_t>> hostFiles;

class HalFile {
 public:
  explicit operator bool() const { return open_; }
  bool isOpen() const { return open_; }
  size_t write(const void* data, size_t n) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    hostFiles[path_].insert(hostFiles[path_].end(), bytes, bytes + n);
    return n;
  }
  int read(void* out, size_t n) {
    const auto& data = hostFiles[path_];
    n = std::min(n, data.size() - std::min(data.size(), pos_));
    std::memcpy(out, data.data() + pos_, n);
    pos_ += n;
    return static_cast<int>(n);
  }
  bool seek(size_t at) {
    pos_ = at;
    return true;
  }
  size_t size() const { return hostFiles.at(path_).size(); }
  bool close() {
    open_ = false;
    return true;
  }
  std::string path_;
  size_t pos_ = 0;
  bool open_ = false;
};

struct HostStorage {
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    hostFiles[path].clear();
    file.path_ = path;
    file.pos_ = 0;
    file.open_ = true;
    return true;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    if (!hostFiles.count(path)) return false;
    file.path_ = path;
    file.pos_ = 0;
    file.open_ = true;
    return true;
  }
  bool remove(const char* path) { return hostFiles.erase(path) > 0; }
};
inline HostStorage Storage;
