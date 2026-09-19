#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
struct OpfIoFaults {
  bool read = false, write = false, seek = false, sync = false, close = false;
  bool openRead = false, openWrite = false;
};
inline OpfIoFaults opfFaults;
class HalFile {
 public:
  std::shared_ptr<std::vector<uint8_t>> bytes;
  size_t offset = 0;
  size_t position() const { return offset; }
  size_t size() const { return bytes ? bytes->size() : 0; }
  int available() const { return size() - std::min(size(), offset); }
  bool seek(size_t pos) { if (opfFaults.seek || pos > size()) return false; offset = pos; return true; }
  int read(void* dst, size_t count) {
    if (opfFaults.read || !bytes) return -1;
    const size_t got = std::min(count, size() - std::min(size(), offset));
    if (got) std::memcpy(dst, bytes->data() + offset, got);
    offset += got;
    return static_cast<int>(got);
  }
  size_t write(const void* src, size_t count) {
    if (opfFaults.write || !bytes) return 0;
    if (offset + count > size()) bytes->resize(offset + count);
    std::memcpy(bytes->data() + offset, src, count);
    offset += count;
    return count;
  }
  bool sync() { return !opfFaults.sync; }
  bool close() { bytes.reset(); offset = 0; return !opfFaults.close; }
  explicit operator bool() const { return bool(bytes); }
};
struct OpfStorage {
  std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    if (opfFaults.openWrite) return false;
    out.bytes = files[path] = std::make_shared<std::vector<uint8_t>>(); out.offset = 0; return true;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& out) {
    if (opfFaults.openRead || !files.count(path)) return false;
    out.bytes = files.at(path); out.offset = 0; return true;
  }
  bool exists(const char* path) const { return files.count(path) != 0; }
  bool remove(const char* path) { return files.erase(path) != 0; }
};
inline OpfStorage Storage;
