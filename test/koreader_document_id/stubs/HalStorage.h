#pragma once
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

class HalFile {
 public:
  bool open(const std::string& path) {
    stream.open(path, std::ios::binary);
    if (!stream) return false;
    stream.seekg(0, std::ios::end);
    size = static_cast<size_t>(stream.tellg());
    stream.seekg(0);
    return true;
  }
  size_t fileSize() const { return size; }
  bool seekSet(size_t position) { stream.clear(); stream.seekg(static_cast<std::streamoff>(position)); return bool(stream); }
  int read(void* buf, size_t count) {
    stream.read(static_cast<char*>(buf), static_cast<std::streamsize>(count));
    return static_cast<int>(stream.gcount());
  }

 private:
  std::ifstream stream;
  size_t size = 0;
};

struct HalStorageStub {
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path); }
};
inline HalStorageStub Storage;
