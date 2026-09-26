#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Files in RAM: the pixel cache the converter writes, read back by the test.
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
  int read(void*, size_t) { return 0; }
  bool seek(size_t) { return true; }
  size_t size() const { return 0; }
  void close() { open_ = false; }
  std::string path_;
  bool open_ = false;
};

struct HostStorage {
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    hostFiles[path].clear();
    file.path_ = path;
    file.open_ = true;
    return true;
  }
  bool openFileForRead(const char*, const std::string&, HalFile&) { return true; }
  bool remove(const char* path) { return hostFiles.erase(path) > 0; }
};
inline HostStorage Storage;
