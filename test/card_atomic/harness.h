#include <RecoverableFile.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#define O_WRONLY 1
#define O_CREAT 2
#define O_TRUNC 4
struct PowerCut {};
struct File;
struct Store {
  std::map<std::string, std::string> files;
  std::string fault, cut, failReadPath;
  int writes = 0, payloadWrites = 0;
  File open(const char*, int = 0);
  bool exists(const char* p) { return files.count(p); }
  bool remove(const char* p) { if (fault == "remove") return false; return files.erase(p); }
  bool rename(const char* a, const char* b) {
    const bool promote = std::string(a).find(".davtmp") != std::string::npos;
    if (fault == (promote ? "promote" : "backup")) return false;
    if (!exists(a) || exists(b)) return false;
    files[b] = files[a]; files.erase(a);
    if (cut == (promote ? "promote" : "backup")) throw PowerCut{};
    return true;
  }
} Storage;
struct File {
  Store* s = nullptr; std::string p; size_t position = 0; bool writable = false;
  explicit operator bool() const { return s != nullptr; }
  size_t size() { return s->files[p].size(); }
  int read(void* out, size_t n) {
    if (s->fault == "read" || s->failReadPath == p) return -1;
    n = std::min(n, size() - position);
    memcpy(out, s->files[p].data() + position, n);
    if (s->fault == "stage" && p.find(".davtmp") != std::string::npos && n) static_cast<char*>(out)[0] ^= 1;
    position += n; return static_cast<int>(n);
  }
  size_t write(const void* in, size_t n) {
    ++s->writes; if (s->writes == 2) ++s->payloadWrites;
    if (s->fault == (s->writes == 1 ? "header" : "payload")) n /= 2;
    s->files[p].append(static_cast<const char*>(in), n);
    if (s->cut == (s->writes == 1 ? "header" : "payload")) throw PowerCut{};
    return n;
  }
  bool sync() { if (s->cut == "sync") throw PowerCut{}; return s->fault != "sync"; }
  bool close() {
    if (!s) return true;
    if (writable && s->cut == "close") throw PowerCut{};
    return s->fault != (writable ? "close" : "readclose");
  }
};
File Store::open(const char* p, int flags) {
  if (fault == "open") return {};
  if (flags) { files[p].clear(); return {this, p, 0, true}; }
  if (!exists(p)) return {};
  return {this, p, 0, false};
}
struct Renderer {
  size_t getRegionByteSize(int, int, int w, int h) { return w > 0 && h > 0 ? static_cast<size_t>(w) * h : 0; }
} renderer;
struct { int uiTextSize = 0; } SETTINGS;
int normalizedUiTextSize(int n) { return n; }
class HomeActivity {
 public:
  enum class CardFile { None, Cover, Whole };
  uint8_t* coverBuffer = nullptr;
  size_t coverBufferSize = 0;
  int coverBufferUiSize = 0;
  int coverRectX = 0, coverRectY = 0, coverRectW = 2, coverRectH = 2;
  int textRectX = 0, textRectY = 2, textRectW = 2, textRectH = 2;
  bool coverBufferStored = true, cardOnCard = false;
  uint32_t cardFileCoverKey = 7, cardFileKey = 8;
  uint8_t cardFileThumb = 1;
  int16_t cardFileCover = 450;
  std::string cardFilePending = "card";
  ~HomeActivity() { freeCoverBuffer(); }
  void freeCoverBuffer() { free(coverBuffer); coverBuffer = nullptr; coverBufferSize = 0; }
  CardFile loadCardFile(const std::string&, uint32_t, uint32_t, const std::string&);
  void saveCardFile();
};
