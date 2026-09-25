#pragma once
#include <Arduino.h>
#include <Print.h>

#include <algorithm>
#include <cstring>
#include <vector>

// A read-only file over bytes in RAM: what the JPEG decoders ask of a cover file.
class HalFile : public Print {
 public:
  HalFile() = default;
  explicit HalFile(const std::vector<uint8_t>& bytes) : data(&bytes) {}
  explicit operator bool() const { return data != nullptr; }
  size_t size() const { return data ? data->size() : 0; }
  bool seek(size_t to) {
    if (!data || to > data->size()) return false;
    pos = to;
    return true;
  }
  int read(void* dst, size_t count) {
    count = std::min(count, size() - pos);
    std::memcpy(dst, data->data() + pos, count);
    pos += count;
    bytesRead += count;
    return static_cast<int>(count);
  }
  size_t bytesRead = 0;

 private:
  const std::vector<uint8_t>* data = nullptr;
  size_t pos = 0;
};
