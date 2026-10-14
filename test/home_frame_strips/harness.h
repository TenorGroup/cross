#include <RecoverableFile.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <HeapFloor.h>
#define free(p) heapfloor::release(p)
#include "FrameStrips.h"
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define O_WRONLY 1
#define O_CREAT 2
#define O_TRUNC 4
struct PowerCut {};
struct File;
struct Store {
  std::map<std::string, std::string> files;
  std::string fault, cut;
  int writes = 0, reads = 0, failReadAt = 0;
  File open(const char*, int = 0);
  bool exists(const char* p) { return files.count(p); }
  bool exists(const std::string& p) { return files.count(p); }
  bool remove(const char* p) { return files.erase(p); }
  bool rename(const char* a, const char* b) {
    if (!exists(a) || exists(b)) return false;
    files[b] = files[a]; files.erase(a);
    return true;
  }
} Storage;
struct File {
  Store* s = nullptr; std::string p; size_t position = 0; bool writable = false;
  explicit operator bool() const { return s != nullptr; }
  size_t size() { return s->files[p].size(); }
  int read(void* out, size_t n) {
    if (s->failReadAt && ++s->reads >= s->failReadAt) return -1;
    n = std::min(n, size() - position);
    memcpy(out, s->files[p].data() + position, n);
    position += n; return static_cast<int>(n);
  }
  size_t write(const void* in, size_t n) {
    ++s->writes;
    if (s->fault == (s->writes == 1 ? "header" : "payload")) n /= 2;
    s->files[p].append(static_cast<const char*>(in), n);
    if (s->cut == (s->writes == 1 ? "header" : "payload")) throw PowerCut{};
    return n;
  }
  bool sync() { return s->fault != "sync"; }
  bool close() { return true; }
};
File Store::open(const char* p, int flags) {
  if (flags) { files[p].clear(); return {this, p, 0, true}; }
  if (!exists(p)) return {};
  return {this, p, 0, false};
}
// The screen buffer as the X3 has it (528 x 792, 1 bit a pixel), the rect turned to bytes as GfxRenderer does.
struct Renderer {
  static constexpr int W = 528, H = 792, ROW = W / 8;
  mutable std::array<uint8_t, ROW * H> fb{};
  int cleared = 0;
  bool bounds(int x, int y, int w, int h, int& byte0, int& bytes, int& y0, int& rows) const {
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > W || y + h > H) return false;
    byte0 = x / 8; bytes = (x + w - 1) / 8 - byte0 + 1; y0 = y; rows = h;
    return true;
  }
  size_t getRegionByteSize(int x, int y, int w, int h) const {
    int b0, bytes, y0, rows;
    return bounds(x, y, w, h, b0, bytes, y0, rows) ? static_cast<size_t>(bytes) * rows : 0;
  }
  bool getRegionRows(int x, int y, int w, int h, int& perRow, int& rowCount) const {
    int b0, y0;
    return bounds(x, y, w, h, b0, perRow, y0, rowCount);
  }
  bool copyRegionRowsToBuffer(int x, int y, int w, int h, int first, int rows, uint8_t* buf, size_t size) const {
    int b0, bytes, y0, count;
    if (!bounds(x, y, w, h, b0, bytes, y0, count) || first < 0 || rows < 0 || first + rows > count ||
        size < static_cast<size_t>(bytes) * rows || !buf) return false;
    for (int r = 0; r < rows; ++r) memcpy(buf + r * bytes, fb.data() + (y0 + first + r) * ROW + b0, bytes);
    return true;
  }
  bool copyBufferToRegionRows(int x, int y, int w, int h, int first, int rows, const uint8_t* buf, size_t size) const {
    int b0, bytes, y0, count;
    if (!bounds(x, y, w, h, b0, bytes, y0, count) || first < 0 || rows < 0 || first + rows > count ||
        size < static_cast<size_t>(bytes) * rows || !buf) return false;
    for (int r = 0; r < rows; ++r) memcpy(fb.data() + (y0 + first + r) * ROW + b0, buf + r * bytes, bytes);
    return true;
  }
  bool copyRegionToBuffer(int x, int y, int w, int h, uint8_t* buf, size_t size) const {
    int perRow, rows;
    return getRegionRows(x, y, w, h, perRow, rows) && copyRegionRowsToBuffer(x, y, w, h, 0, rows, buf, size);
  }
  bool copyBufferToRegion(int x, int y, int w, int h, const uint8_t* buf, size_t size) const {
    int perRow, rows;
    return getRegionRows(x, y, w, h, perRow, rows) && copyBufferToRegionRows(x, y, w, h, 0, rows, buf, size);
  }
  void fillRect(int x, int y, int w, int h, bool state = true) {
    ++cleared;
    for (int yy = y; yy < y + h; ++yy)
      for (int xx = x; xx < x + w; ++xx) {
        uint8_t& byte = fb[yy * ROW + xx / 8];
        const uint8_t bit = 0x80 >> (xx % 8);
        byte = state ? byte & ~bit : byte | bit;
      }
  }
} gRenderer;
struct { int uiTextSize = 0; int homeStyle = 1; } SETTINGS;
int normalizedUiTextSize(int n) { return n; }
namespace tenorchrome { constexpr bool kTouchShell = false; bool enabled() { return true; } }
namespace HalMemory {
struct PsramBuffer {
  uint8_t* get() { return nullptr; }
  void reset() {}
  explicit operator bool() const { return false; }
};
inline PsramBuffer allocatePsram(size_t) { return {}; }
}
class HomeActivity {
 public:
  enum class CardFile : uint8_t { None, Cover, Whole };
  Renderer& renderer = gRenderer;
  uint8_t* coverBuffer = nullptr;
  HalMemory::PsramBuffer touchCoverBuffer;
  size_t coverBufferSize = 0;
  uint8_t coverBufferUiSize = 0, coverBufferStyle = 0;
  int coverRectX = 0, coverRectY = 0, coverRectW = 0, coverRectH = 0;
  int textRectX = 0, textRectY = 0, textRectW = 0, textRectH = 0;
  int coverBufferBook = -1;
  bool coverBufferStored = false, cardOnCard = false;
  framestrip::Snapshot frameSnapshot;
  std::string cardFilePending;
  uint32_t cardFileCoverKey = 0, cardFileKey = 0;
  uint8_t cardFileThumb = 0;
  int16_t cardFileCover = 0;
  int touchThumbCount = 0;
  ~HomeActivity() { freeCoverBuffer(); }
  bool stylesAvailable() const { return true; }
  bool frameOnCard() const;
  bool storeCoverBuffer();
  bool restoreCoverBuffer();
  void freeCoverBuffer();
  CardFile loadCardFile(const std::string&, uint32_t, uint32_t, const std::string&, bool = false);
  void saveCardFile();
  void holdFrame(const std::string&, uint32_t, bool);
  bool restoreFrame(const std::string&, uint32_t);
};
