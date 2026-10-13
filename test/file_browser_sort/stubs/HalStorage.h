#pragma once
// Host stand-in for the card: one folder of dirents with a date and a size each.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fakecard {
struct Entry {
  std::string name;
  bool directory = false;
  uint32_t modified = 0;
  uint64_t size = 0;
};
inline std::string folder = "/";
inline std::vector<Entry> entries;
}  // namespace fakecard

class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(int index) : index(index) {}
  explicit operator bool() const { return index != kNone; }
  bool isDirectory() const { return index == kFolder || (index >= 0 && fakecard::entries[index].directory); }
  void rewindDirectory() { next = 0; }
  HalFile openNextFile() {
    return next < static_cast<int>(fakecard::entries.size()) ? HalFile(next++) : HalFile();
  }
  size_t getName(char* name, size_t len) {
    const auto& text = fakecard::entries[index].name;
    const size_t n = text.size() < len ? text.size() : len - 1;
    std::memcpy(name, text.data(), n);
    name[n] = 0;
    return n;
  }
  uint32_t modificationTime() { return fakecard::entries[index].modified; }
  uint64_t fileSize64() { return fakecard::entries[index].size; }
  bool close() { return true; }

  static constexpr int kNone = -2, kFolder = -1;

 private:
  int index = kNone;
  int next = 0;
};

struct FakeStorage {
  HalFile open(const char* path) { return fakecard::folder == path ? HalFile(HalFile::kFolder) : HalFile(); }
};
inline FakeStorage Storage;
