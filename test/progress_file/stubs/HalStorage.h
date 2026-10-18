#pragma once
// A card in memory that stops answering after a set number of changes, as a card that dies mid-save.
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

struct FakeCard {
  std::map<std::string, std::string> files;
  int changesLeft = 1 << 30;  // each create, rename and remove spends one; at 0 the card is dead
  bool spend() { return changesLeft-- > 0; }
  bool alive() const { return changesLeft > 0; }
};
inline FakeCard card;

class HalFile {
 public:
  std::string path;
  size_t pos = 0;
  bool open = false, writable = false;
  std::string pending;
  ~HalFile() { close(); }
  size_t write(const uint8_t* b, size_t n) {
    if (!card.alive()) return 0;
    pending.append(reinterpret_cast<const char*>(b), n);
    return n;
  }
  void flush() {}
  bool close() {
    if (open && writable && card.spend()) card.files[path] = pending;
    open = false;
    return true;
  }
  int read(void* out, size_t n) {
    if (!open || !card.alive()) return -1;
    const std::string& d = card.files[path];
    n = std::min(n, d.size() - pos);
    d.copy(static_cast<char*>(out), n, pos);
    pos += n;
    return static_cast<int>(n);
  }
};

struct HalStorage {
  bool exists(const char* p) { return card.alive() && card.files.count(p); }
  bool remove(const char* p) { return exists(p) && card.spend() && card.files.erase(p); }
  bool rename(const char* a, const char* b) {
    if (!exists(a) || exists(b) || !card.spend()) return false;
    card.files[b] = card.files[a];
    card.files.erase(a);
    return true;
  }
  bool openFileForWrite(const char*, const std::string& p, HalFile& f) {
    if (!card.alive()) return false;
    f.close();
    f = HalFile{};
    f.path = p;
    f.open = f.writable = true;
    return true;
  }
  bool openFileForRead(const char*, const std::string& p, HalFile& f) {
    if (!exists(p.c_str())) return false;
    f.close();
    f = HalFile{};
    f.path = p;
    f.open = true;
    return true;
  }
};
inline HalStorage Storage;
