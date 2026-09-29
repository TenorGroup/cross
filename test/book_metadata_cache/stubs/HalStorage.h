#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

// In-memory SD card. Counts the calls that cost an SD transaction on the device.
struct CardCalls {
  size_t opens = 0;
  size_t reads = 0;
  size_t seeks = 0;
};
inline CardCalls cardCalls;

class HalFile {
 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  explicit operator bool() const { return static_cast<bool>(data); }
  size_t position() const { return pos; }
  size_t size() const { return data ? data->size() : 0; }

  bool seek(const size_t target) {
    cardCalls.seeks++;
    if (!data || target > data->size()) return false;
    pos = target;
    return true;
  }

  int read(void* dst, const size_t len) {
    cardCalls.reads++;
    if (!data) return -1;
    const size_t got = pos < data->size() ? std::min(len, data->size() - pos) : 0;
    if (got > 0) memcpy(dst, data->data() + pos, got);
    pos += got;
    return static_cast<int>(got);
  }

  size_t write(const void* src, const size_t len) {
    if (!data) return 0;
    if (pos + len > data->size()) data->resize(pos + len);
    memcpy(data->data() + pos, src, len);
    pos += len;
    return len;
  }
  size_t write(const uint8_t* src, const size_t len) { return write(static_cast<const void*>(src), len); }

  bool close() {
    data.reset();
    pos = 0;
    return true;
  }

 private:
  friend class HalStorage;
  std::shared_ptr<std::vector<uint8_t>> data;
  size_t pos = 0;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    cardCalls.opens++;
    file.close();
    const auto it = files.find(path);
    if (it == files.end()) return false;
    file.data = it->second;
    return true;
  }

  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    file.close();
    file.data = files[path] = std::make_shared<std::vector<uint8_t>>();
    return true;
  }

  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) { return files.erase(path) != 0; }
  void clear() { files.clear(); }

 private:
  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
};

#define Storage HalStorage::getInstance()
