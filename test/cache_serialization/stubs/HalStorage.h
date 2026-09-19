#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
class HalFile {
 public:
  std::vector<uint8_t> bytes;
  size_t offset = 0;
  size_t readLimit = SIZE_MAX;
  size_t writeLimit = SIZE_MAX;
  size_t calls = 0;
  size_t failAtCall = SIZE_MAX;
  size_t writeCalls = 0;
  size_t failAtWriteCall = SIZE_MAX;
  int read(void* dst, size_t count) {
    if (calls++ == failAtCall) return -1;
    const size_t got = std::min({count, readLimit, bytes.size() - std::min(offset, bytes.size())});
    if (got) std::memcpy(dst, bytes.data() + offset, got);
    offset += got;
    return static_cast<int>(got);
  }
  size_t write(const void* src, size_t count) {
    if (writeCalls++ == failAtWriteCall) return 0;
    const size_t got = std::min(count, writeLimit);
    const auto* data = static_cast<const uint8_t*>(src);
    bytes.insert(bytes.end(), data, data + got);
    offset += got;
    return got;
  }
  size_t position() const { return offset; }
  size_t size() const { return bytes.size(); }
  bool seek(size_t target) { offset = target; return target <= bytes.size(); }
};
