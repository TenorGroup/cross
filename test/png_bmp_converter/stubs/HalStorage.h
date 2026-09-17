#pragma once
#include <Print.h>

#include <algorithm>
#include <cstring>

class HalFile {
 public:
  HalFile(const uint8_t* data, size_t size) : data(data), length(size) {}
  int read(void* dst, size_t count) {
    count = std::min(count, length - position);
    std::memcpy(dst, data + position, count);
    position += count;
    return static_cast<int>(count);
  }
  bool seekCur(size_t count) {
    if (count > length - position) return false;
    position += count;
    return true;
  }

 private:
  const uint8_t* data;
  size_t length;
  size_t position = 0;
};
