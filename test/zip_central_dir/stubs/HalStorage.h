#pragma once
#include <Print.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Files in RAM. Every call a real HalFile makes under the card's lock is counted in `calls`.
struct StorageCalls {
  size_t calls = 0, reads = 0, bytes = 0;
};
inline StorageCalls storageCalls;

class HalFile : public Print {
 public:
  HalFile() = default;
  explicit HalFile(std::shared_ptr<const std::vector<uint8_t>> bytes) : data(std::move(bytes)) {}
  explicit operator bool() const { return data != nullptr; }
  size_t size() const { return data ? data->size() : 0; }
  bool seek(size_t to) {
    storageCalls.calls++;
    if (!data || to > data->size()) return false;
    pos = to;
    return true;
  }
  bool seekCur(int64_t by) {
    storageCalls.calls++;
    const int64_t to = static_cast<int64_t>(pos) + by;
    if (!data || to < 0 || to > static_cast<int64_t>(data->size())) return false;
    pos = static_cast<size_t>(to);
    return true;
  }
  int available() const {
    storageCalls.calls++;
    return data ? static_cast<int>(data->size() - pos) : 0;
  }
  size_t position() const {
    storageCalls.calls++;
    return pos;
  }
  int read(void* dst, size_t count) {
    storageCalls.calls++;
    storageCalls.reads++;
    if (!data) return -1;
    count = std::min(count, data->size() - pos);
    std::memcpy(dst, data->data() + pos, count);
    pos += count;
    storageCalls.bytes += count;
    return static_cast<int>(count);
  }
  void close() { data.reset(); }

 private:
  std::shared_ptr<const std::vector<uint8_t>> data;
  size_t pos = 0;
};

class HalStorage {
 public:
  std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> files;
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    const auto it = files.find(path);
    if (it == files.end()) return false;
    file = HalFile(it->second);
    return true;
  }
};
inline HalStorage Storage;
