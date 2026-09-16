#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
struct TestFile {
  std::vector<uint8_t> bytes;
  size_t readable = SIZE_MAX;
  size_t reads = 0, seeks = 0;
  size_t seekFailureAt = SIZE_MAX;
};
class HalFile {
 public:
  std::shared_ptr<TestFile> data;
  size_t pos = 0;
  explicit operator bool() const { return bool(data); }
  size_t position() const { return pos; }
  size_t size() const { return data ? data->bytes.size() : 0; }
  bool seek(size_t p) {
    if (data) ++data->seeks;
    if (!data || p >= data->seekFailureAt || p > size()) return false;
    pos = p;
    return true;
  }
  int read(void* dst, size_t n) {
    if (!data) return -1;
    ++data->reads;
    const size_t end = std::min(size(), data->readable);
    const size_t got = pos < end ? std::min(n, end - pos) : 0;
    if (got) memcpy(dst, data->bytes.data() + pos, got);
    pos += got;
    return static_cast<int>(got);
  }
  size_t write(const void* src, size_t n) {
    if (!data) return 0;
    if (pos + n > size()) data->bytes.resize(pos + n);
    memcpy(data->bytes.data() + pos, src, n);
    pos += n;
    return n;
  }
  void close() {
    if (!data) throw std::logic_error("HalFile::close on null implementation");
    data.reset();
    pos = 0;
  }
};
struct TestStorage {
  std::map<std::string, std::shared_ptr<TestFile>> files;
  bool openFileForRead(const char*, const std::string& path, HalFile& out) {
    if (out) out.close();
    auto it = files.find(path);
    if (it == files.end()) return false;
    out.data = it->second;
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    if (out) out.close();
    out.data = files[path] = std::make_shared<TestFile>();
    return true;
  }
  bool exists(const char* p) const { return files.count(p); }
  bool remove(const char* p) { return files.erase(p); }
};
inline TestStorage Storage;
