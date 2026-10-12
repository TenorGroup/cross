#pragma once
#include <Print.h>
#include "../../huge_book/HeapCapState.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct StorageCalls {
  size_t calls = 0, reads = 0, bytes = 0, seeks = 0, writes = 0, written = 0, opens = 0;
  size_t indexReads = 0, indexBytes = 0, eocdReads = 0;
};
inline StorageCalls storageCalls;
struct StorageFaults {
  bool failOpen = false, failSync = false, failClose = false, failRename = false;
  size_t writeBytesLeft = SIZE_MAX;
  size_t failRenameCall = SIZE_MAX, renameCalls = 0;
  std::string failReadPath, failSeekPath;
};
inline StorageFaults storageFaults;

class HalFile : public Print {
 public:
  HalFile() = default;
  HalFile(std::shared_ptr<std::vector<uint8_t>> bytes, std::string path, bool write = false)
      : data(std::move(bytes)), path(std::move(path)), writable(write) {}
  explicit operator bool() const { return data != nullptr; }
  size_t size() const { return data ? data->size() : 0; }
  bool seek(size_t to) {
    ++storageCalls.calls;
    ++storageCalls.seeks;
    if (!data || path == storageFaults.failSeekPath || (!writable && to > data->size())) return false;
    pos = to;
    return true;
  }
  bool seekCur(int64_t by) { return by >= -static_cast<int64_t>(pos) && seek(pos + by); }
  int available() const { ++storageCalls.calls; return data ? static_cast<int>(size() - std::min(pos, size())) : 0; }
  size_t position() const { ++storageCalls.calls; return pos; }
  int read(void* dst, size_t count) {
    ++storageCalls.calls;
    ++storageCalls.reads;
    if (!data || path == storageFaults.failReadPath) return -1;
    count = std::min(count, size() - std::min(pos, size()));
    if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".zidx") == 0) {
      ++storageCalls.indexReads;
      storageCalls.indexBytes += count;
    } else if (count == 1024 && pos + count == size()) {
      ++storageCalls.eocdReads;
    }
    if (count) std::memcpy(dst, data->data() + pos, count);
    pos += count;
    storageCalls.bytes += count;
    return static_cast<int>(count);
  }
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* src, size_t count) override {
    ++storageCalls.calls;
    ++storageCalls.writes;
    if (!data || !writable) return 0;
    count = std::min(count, storageFaults.writeBytesLeft);
    if (storageFaults.writeBytesLeft != SIZE_MAX) storageFaults.writeBytesLeft -= count;
    heapcap::Untracked guard;
    if (pos + count > size()) data->resize(pos + count);
    if (count) std::memcpy(data->data() + pos, src, count);
    pos += count;
    storageCalls.written += count;
    return count;
  }
  bool close() {
    heapcap::Untracked guard;
    data.reset();
    return !writable || !storageFaults.failClose;
  }
  bool sync() { return data && !storageFaults.failSync; }

 private:
  std::shared_ptr<std::vector<uint8_t>> data;
  std::string path;
  bool writable = false;
  size_t pos = 0;
};

class HalStorage {
 public:
  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    heapcap::Untracked guard;
    ++storageCalls.opens;
    const auto it = files.find(path);
    if (it == files.end()) return false;
    file = HalFile(it->second, path);
    return true;
  }
  bool exists(const char* path) const { return files.find(path) != files.end(); }
  bool remove(const char* path) { heapcap::Untracked guard; return files.erase(path) != 0; }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    heapcap::Untracked guard;
    ++storageCalls.opens;
    if (storageFaults.failOpen) return false;
    auto bytes = std::make_shared<std::vector<uint8_t>>();
    files[path] = bytes;
    file = HalFile(bytes, path, true);
    return true;
  }
  bool rename(const char* from, const char* to) {
    heapcap::Untracked guard;
    const size_t call = ++storageFaults.renameCalls;
    if (storageFaults.failRename || call == storageFaults.failRenameCall || !exists(from) || exists(to)) return false;
    files[to] = files.at(from);
    files.erase(from);
    return true;
  }
};
inline HalStorage Storage;
