#pragma once

#include <algorithm>
#include <cstring>
#include <string>

#include <Print.h>

class FsFile {
 public:
  FsFile() = default;
  explicit FsFile(const char* path) : name(path), content("abcd"), opened(true) {}
  bool isOpen() const { return opened; }
  bool close() { opened = false; return true; }
  void flush() {}
  bool sync() { return name != "/badsync"; }
  size_t getName(char* dst, size_t size) const {
    if (size == 0) return 0;
    const size_t count = std::min(size - 1, name.size());
    std::memcpy(dst, name.data(), count);
    dst[count] = '\0';
    return count;
  }
  size_t size() const { return content.size(); }
  size_t fileSize() const { return content.size(); }
  bool seekSet(uint64_t pos) { if (pos > content.size()) return false; cursor = pos; return true; }
  bool seekCur(int64_t delta) {
    if (delta < 0 && static_cast<uint64_t>(-delta) > cursor) return false;
    return seekSet(static_cast<uint64_t>(static_cast<int64_t>(cursor) + delta));
  }
  int available() const { return static_cast<int>(content.size() - cursor); }
  size_t position() const { return cursor; }
  int read(void* dst, size_t count) {
    const size_t actual = std::min(count, content.size() - cursor);
    std::memcpy(dst, content.data() + cursor, actual);
    cursor += actual;
    return static_cast<int>(actual);
  }
  int read() { return cursor < content.size() ? static_cast<unsigned char>(content[cursor++]) : -1; }
  size_t write(const uint8_t* src, size_t count) { return append(src, count); }
  size_t write(const void* src, size_t count) { return append(src, count); }
  size_t write(uint8_t value) { return append(&value, 1); }
  // SdFat's FAT timestamp pair; the stub files carry no date (0 = unknown).
  bool getModifyDateTime(uint16_t* date, uint16_t* time) const {
    *date = 0;
    *time = 0;
    return opened;
  }
  bool rename(const char* path) { name = path; return true; }
  bool isDirectory() const { return name == "/dir"; }
  void rewindDirectory() { cursor = 0; }
  FsFile openNextFile() { return FsFile("child.epub"); }

 private:
  size_t append(const void* src, size_t count) {
    const size_t actual = name == "/shortwrite" ? std::min<size_t>(count, 2) : count;
    content.append(static_cast<const char*>(src), actual);
    return actual;
  }
  std::string name;
  std::string content;
  size_t cursor = 0;
  bool opened = false;
};
