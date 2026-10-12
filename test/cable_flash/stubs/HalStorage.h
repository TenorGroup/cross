#pragma once
// The card holds one file, the image under test.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
class HalFile {
 public:
  const std::vector<uint8_t>* data = nullptr;
  size_t at = 0;
  explicit operator bool() const { return data != nullptr; }
  size_t fileSize() { return data->size(); }
  int read(void* buf, size_t count) {
    const size_t n = std::min(count, data->size() - at);
    std::memcpy(buf, data->data() + at, n);
    at += n;
    return static_cast<int>(n);
  }
  void close() { data = nullptr; }
};
struct FakeStorage {
  std::vector<uint8_t> file;
  bool openFileForRead(const char*, const char*, HalFile& f) {
    f.data = &file;
    f.at = 0;
    return true;
  }
};
extern FakeStorage Storage;
