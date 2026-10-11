#!/usr/bin/env python3
"""The cover BMP of a book (cover_jpeg2.bmp on PSRAM boards) is written under a part name and renamed once whole.
generateCoverBmp treats an existing name as "already generated", so a write cut by power loss that leaves a short
file under the final name serves a truncated cover forever. Runs the production Epub::getCoverBmpPath and
Epub::generateCoverBmp with a storage that cuts the power at each step of the write.

  test_cover_bmp_atomic.py [--epub lib/Epub/Epub.cpp]
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--epub', type=Path, default=repo / 'lib/Epub/Epub.cpp')
source = parser.parse_args().epub.read_text()
body = source[source.index('std::string Epub::getCoverBmpPath('):source.index('// Keep thumb2 caches')]
stubs = r'''
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <RecoverableFile.h>
struct PowerCut {};
std::string cut;  // "write": half the BMP is on the card; "rename": before the move; "renamed": after it
bool convertOk = true;
int converted = 0;
const std::string kBmp(64, 'B');
struct HalFile {
  std::string* data = nullptr;
  explicit operator bool() const { return data != nullptr; }
  void close() { data = nullptr; }
  size_t size() const { return data ? data->size() : 0; }
};
struct Store {
  std::map<std::string, std::string> files;
  bool exists(const char* p) const { return files.count(p); }
  bool remove(const char* p) { return files.erase(p); }
  bool openFileForWrite(const char*, const std::string& p, HalFile& f) { files[p].clear(); f.data = &files[p]; return true; }
  bool openFileForRead(const char*, const std::string& p, HalFile& f) { if (!exists(p.c_str())) return false; f.data = &files[p]; return true; }
  bool rename(const char* a, const char* b) {
    if (!exists(a) || exists(b)) return false;
    if (cut == "rename") throw PowerCut{};
    files[b] = files[a]; files.erase(a);
    if (cut == "renamed") throw PowerCut{};
    return true;
  }
} Storage;
namespace progressivejpeg { bool supported() { return true; } }
struct FsHelpers {
  static bool hasJpgExtension(const std::string& s) { return s.ends_with(".jpg"); }
  static bool hasPngExtension(const std::string& s) { return s.ends_with(".png"); }
};
bool convert(HalFile& dst) {
  ++converted;
  dst.data->append(kBmp, 0, kBmp.size() / 2);
  if (cut == "write") throw PowerCut{};
  dst.data->append(kBmp, kBmp.size() / 2, std::string::npos);
  return convertOk;
}
struct JpegToBmpConverter {
  static bool jpegFileToBmpStream(HalFile&, HalFile& dst, bool, bool, bool) { return convert(dst); }
};
struct PngToBmpConverter {
  static bool pngFileToBmpStream(HalFile&, HalFile& dst, bool, bool, bool) { return convert(dst); }
};
unsigned long millis() { return 0; }
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
struct Meta { std::string coverItemHref; };
struct Cache { Meta coreMetadata; bool isLoaded() const { return true; } };
struct Epub {
  std::string cachePath = "/.crosspoint/epub_123";
  Cache* bookMetadataCache = nullptr;
  std::string getCachePath() const { return cachePath; }
  bool readItemContentsToStream(const std::string&, HalFile& f, int) const { *f.data = "cover-image"; return true; }
  std::string getCoverBmpPath(bool cropped = false, bool originalThresholds = false, bool oneBit = false) const;
  bool generateCoverBmp(bool cropped = false, bool originalThresholds = false, bool oneBit = false) const;
};
'''
main = r'''
int main() {
  int cases = 0;
  for (const char* href : {"images/cover.jpg", "images/cover.png"}) {
    Cache cache{{href}};
    Epub epub; epub.bookMetadataCache = &cache;
    const std::string path = epub.getCoverBmpPath();
    assert(path.ends_with("_jpeg2.bmp"));
    // A whole run leaves the cover and nothing else beside it.
    Storage.files.clear(); cut.clear(); convertOk = true;
    assert(epub.generateCoverBmp() && Storage.files.at(path) == kBmp);
    for (const auto& [name, data] : Storage.files) assert(name == path);
    ++cases;
    // A converter that fails leaves no cover and no part.
    Storage.files.clear(); convertOk = false;
    assert(!epub.generateCoverBmp() && Storage.files.empty());
    convertOk = true;
    ++cases;
    // Power cut at each step: the final name is absent or whole, and the next boot ends with a whole cover.
    for (const char* point : {"write", "rename", "renamed"}) {
      Storage.files.clear(); cut = point;
      try { epub.generateCoverBmp(); assert(false && "the cut did not fire"); } catch (const PowerCut&) {}
      cut.clear();
      if (Storage.exists(path.c_str())) assert(Storage.files.at(path) == kBmp && "a truncated cover holds the final name");
      converted = 0;
      assert(epub.generateCoverBmp() && Storage.files.at(path) == kBmp);
      for (const auto& [name, data] : Storage.files) assert(name == path || name.ends_with(".davtmp"));
      ++cases;
    }
  }
  std::printf("GREEN %d cover bmp cases: no truncated cover under the final name, the next boot rebuilds it whole\n", cases);
}
'''
with tempfile.TemporaryDirectory(prefix='cover-bmp-') as tmp:
    cpp = Path(tmp) / 'cover.cpp'
    cpp.write_text(stubs + body + main)
    exe = Path(tmp) / 'cover'
    subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-DBOARD_HAS_PSRAM=1',
                    '-I' + str(repo / 'freeink-sdk/libs/hardware/SDCardManager/include'),
                    str(cpp), '-o', str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
