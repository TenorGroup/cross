#pragma once
// A card in RAM for the cover job: files by path, a card that fills up, and a power cut at any
// one change (the card keeps every change made before it).
#include <Arduino.h>
#include <Print.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

struct PowerCut {};

class HalStorage;
class HalFile : public Print {
 public:
  HalFile() = default;
  HalFile(HalStorage* s, std::string p, bool w) : store(s), path(std::move(p)), writable(w) {}
  explicit HalFile(const std::vector<uint8_t>& bytes);
  explicit operator bool() const { return store != nullptr || view != nullptr; }
  size_t size() const;
  bool seek(size_t to) {
    if (to > size()) return false;
    pos = to;
    return true;
  }
  int read(void* dst, size_t count);
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* p, size_t n) override;
  size_t write(const void* p, size_t n) { return write(static_cast<const uint8_t*>(p), n); }
  bool sync();
  bool close();

 private:
  HalStorage* store = nullptr;
  const std::vector<uint8_t>* view = nullptr;
  std::string path;
  size_t pos = 0;
  bool writable = false, cut = false;
};

class HalStorage {
 public:
  std::map<std::string, std::vector<uint8_t>> files;
  size_t capacity = SIZE_MAX;  // bytes the card holds
  size_t used() const {
    size_t n = 0;
    for (const auto& f : files) n += f.second.size();
    return n;
  }
  int changes = 0, cutAt = 0, readFails = 0;
  // Reads of one file, and the read of it that fails (0: none).
  std::string watchPath;
  int watchReads = 0, watchFailAt = 0;
  std::vector<std::string> log;
  // Every change counts; the cutAt-th one never happens.
  void change(const std::string& what) {
    ++changes;
    log.push_back(what);
    if (cutAt && changes == cutAt) throw PowerCut{};
  }
  bool exists(const char* p) { return files.count(p) > 0; }
  bool remove(const char* p) {
    if (!exists(p)) return false;
    change("remove " + std::string(p));
    files.erase(p);
    return true;
  }
  bool rename(const char* a, const char* b) {
    if (!exists(a) || exists(b)) return false;  // FAT: the new name must be free
    change("rename " + std::string(a) + " " + b);
    files[b] = std::move(files[a]);
    files.erase(a);
    return true;
  }
  HalFile open(const char* p, int = O_RDONLY) { return exists(p) ? HalFile(this, p, false) : HalFile(); }
  bool openFileForRead(const char*, const std::string& p, HalFile& f) {
    f = open(p.c_str());
    return static_cast<bool>(f);
  }
  bool openFileForWrite(const char*, const std::string& p, HalFile& f) {
    change("create " + p);
    files[p].clear();
    f = HalFile(this, p, true);
    return true;
  }
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }
};
#define Storage HalStorage::getInstance()

inline HalFile::HalFile(const std::vector<uint8_t>& bytes) : view(&bytes) {}
inline size_t HalFile::size() const { return view ? view->size() : store ? store->files[path].size() : 0; }
inline int HalFile::read(void* dst, size_t count) {
  if (store && store->readFails && --store->readFails == 0) return -1;
  if (store && path == store->watchPath && ++store->watchReads == store->watchFailAt) return -1;
  const auto& b = view ? *view : store->files[path];
  count = std::min(count, b.size() - std::min(pos, b.size()));
  std::memcpy(dst, b.data() + pos, count);
  pos += count;
  return static_cast<int>(count);
}
inline size_t HalFile::write(const uint8_t* p, size_t n) {
  if (!store || !writable) return 0;
  store->change("write " + path);
  const size_t room = store->capacity - std::min(store->capacity, store->used());
  if (n > room) cut = true;
  n = std::min(n, room);
  auto& b = store->files[path];
  b.insert(b.end(), p, p + n);
  return n;
}
inline bool HalFile::sync() { return store && !cut; }
inline bool HalFile::close() {
  if (store && writable) store->change("close " + path);
  const bool ok = !cut;
  store = nullptr;
  view = nullptr;
  return ok;
}
