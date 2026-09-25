#pragma once
// In-memory SD card. Its buffers are test storage, not device heap, so they
// are allocated outside the heap cap.
#include <HeapCapState.h>

#include <fcntl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct TestFile {
  std::vector<uint8_t> bytes;
};
// Card calls, each an SD transaction's worth of overhead on the device.
struct CardCalls {
  size_t reads = 0, writes = 0, seeks = 0;
};
inline CardCalls cardCalls;
class HalFile {
 public:
  std::shared_ptr<TestFile> data;
  size_t pos = 0;
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept : data(std::move(other.data)), pos(other.pos) {}
  HalFile& operator=(HalFile&& other) noexcept {
    heapcap::Untracked guard;
    data = std::move(other.data);
    pos = other.pos;
    return *this;
  }
  ~HalFile() {
    heapcap::Untracked guard;
    data.reset();
  }
  explicit operator bool() const { return bool(data); }
  size_t position() const { return pos; }
  size_t size() const { return data ? data->bytes.size() : 0; }
  int available() const { return static_cast<int>(size() - std::min(size(), pos)); }
  bool seek(size_t p) {
    ++cardCalls.seeks;
    if (!data || p > size()) return false;
    pos = p;
    return true;
  }
  bool seekCur(long delta) { return seek(pos + delta); }
  int read(void* dst, size_t n) {
    ++cardCalls.reads;
    if (!data) return -1;
    const size_t got = pos < size() ? std::min(n, size() - pos) : 0;
    if (got) memcpy(dst, data->bytes.data() + pos, got);
    pos += got;
    return static_cast<int>(got);
  }
  size_t write(const void* src, size_t n) {
    ++cardCalls.writes;
    if (!data) return 0;
    heapcap::Untracked guard;
    if (pos + n > size()) data->bytes.resize(pos + n);
    memcpy(data->bytes.data() + pos, src, n);
    pos += n;
    return n;
  }
  bool sync() { return true; }
  bool close() {
    heapcap::Untracked guard;
    data.reset();
    pos = 0;
    return true;
  }
};
struct TestStorage {
  std::map<std::string, std::shared_ptr<TestFile>> files;
  bool openFileForRead(const char*, const std::string& path, HalFile& out) {
    heapcap::Untracked guard;
    out.close();
    auto it = files.find(path);
    if (it == files.end()) return false;
    out.data = it->second;
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    heapcap::Untracked guard;
    out.close();
    out.data = files[path] = std::make_shared<TestFile>();
    return true;
  }
  bool exists(const char* p) const { return files.count(p) != 0; }
  // O_RDWR on a file that exists: kept as it is, at position 0.
  HalFile open(const char* path, int) {
    heapcap::Untracked guard;
    HalFile out;
    auto it = files.find(path);
    if (it != files.end()) out.data = it->second;
    return out;
  }
  bool remove(const char* p) {
    heapcap::Untracked guard;
    return files.erase(p) != 0;
  }
  // Like SdFat: the new name must not exist yet.
  bool rename(const char* from, const char* to) {
    heapcap::Untracked guard;
    auto it = files.find(from);
    if (it == files.end() || files.count(to)) return false;
    files[to] = it->second;
    files.erase(it);
    return true;
  }
};
inline TestStorage Storage;
