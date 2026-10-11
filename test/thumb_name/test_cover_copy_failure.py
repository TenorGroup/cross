#!/usr/bin/env python3
"""A cover copy that fails (the book's ZIP entry cannot be read, or the part file cannot be opened) leaves no
.tmp and no temporary cover, and closes only the files it opened: the production storage asserts on closing a
file that was never opened. Runs the production Epub::generateThumbBmps with a storage that keeps that assert.

  test_cover_copy_failure.py [--epub lib/Epub/Epub.cpp]
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--epub', type=Path, default=repo / 'lib/Epub/Epub.cpp')
source = parser.parse_args().epub.read_text()
paths = source[source.index('std::string Epub::getThumbBmpPath()'):source.index('bool Epub::isCoverImage')]
start = source.index('void Epub::generateThumbBmps(const std::string&')
body = source[start:source.index('// One height,', start)]
stubs = r'''
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include "util/ThumbName.h"
#include "TenorInk.h"
int decodes=0;
bool readOk=true, partOpens=true;
// As lib/hal/HalStorage.cpp: close() asserts the file was opened.
struct HalFile {
  std::vector<unsigned char>* data=nullptr;
  explicit operator bool() const { return data!=nullptr; }
  void close() { assert(data!=nullptr && "close() on a file that was never opened"); data=nullptr; }
  size_t size() const { return data?data->size():0; }
};
struct Store {
  std::map<std::string,std::vector<unsigned char>> files;
  bool exists(const char* p) const { return files.count(p); }
  bool remove(const char* p) { return files.erase(p); }
  bool openFileForWrite(const char*,const std::string& p,HalFile& f) {
    if(!partOpens && p.ends_with(".tmp")) return false;
    f.data=&files[p]; return true;
  }
  bool openFileForRead(const char*,const std::string& p,HalFile& f) { if(!exists(p.c_str())) return false; f.data=&files[p]; return true; }
  bool rename(const char* a,const char* b) { files[b]=files[a]; files.erase(a); return true; }
} Storage;
namespace coverink { bool restore(const std::string&) { return true; } }  // the ink backup: test/cover_ink
struct GrayThumb { explicit GrayThumb(int) {} void alsoFeed(GrayThumb*) {} bool ready() const { return true; }
  bool writeTo(HalFile& f) const { *f.data={1}; return true; } bool writeScaled(int,HalFile& f) const { return writeTo(f); } };
struct FsHelpers { static bool hasJpgExtension(const std::string& s) { return s.ends_with(".jpg"); }
  static bool hasPngExtension(const std::string& s) { return s.ends_with(".png"); } };
struct JpegToBmpConverter {
  static bool jpegFileToInkBmpStream(HalFile&,HalFile& f,int,int,bool) { ++decodes; *f.data={4}; return true; }
  static bool jpegFileToGrayThumb(HalFile&,GrayThumb&,int*) { ++decodes; return true; }
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile&,HalFile& f,int,int,bool=false) { ++decodes; *f.data={1}; return true; }
};
struct PngToBmpConverter {
  static bool pngFileToInkBmpStream(HalFile& c,HalFile& f,int w,int h,bool four) { return JpegToBmpConverter::jpegFileToInkBmpStream(c,f,w,h,four); }
  static bool pngFileTo1BitBmpStreamWithSize(HalFile& c,HalFile& f,int w,int h,bool=false) { return JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(c,f,w,h); }
};
struct EspStub { unsigned getFreeHeap() const { return 1000000; } unsigned getMaxAllocHeap() const { return 1000000; } } ESP;
unsigned long millis() { return 0; }
int thumbWidthFor(int h) { return h*298/450; }
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_PROBE(...) ((void)0)
struct Epub {
  std::string cachePath="/.crosspoint/epub_123";
  std::string getCachePath() const { return cachePath; }
  std::string getThumbBmpPath() const;
  std::string getThumbBmpPath(int) const;
  std::string getThumbBmpPath(int,int) const;
  // The ZIP entry is copied in 8 KiB pieces; a broken entry writes a part of it, then fails.
  bool readItemContentsToStream(const std::string&,HalFile& f,int) const { *f.data={9}; return readOk; }
  void generateThumbBmps(const std::string&,const int*,int) const;
  void generateThumbBmps(const std::string&,const int*,const int*,int,bool=false,bool=true) const;
};
'''
main = r'''
int main() {
  Epub epub;
  const int widths[]={232,253}, heights[]={350,381};
  int cases=0;
  for (bool ink : {false,true}) for (const char* href : {"images/cover.jpg","images/cover.png"}) {
    // The copy out of the book fails: nothing opened, nothing left.
    Storage.files.clear(); readOk=false; partOpens=true; decodes=0;
    epub.generateThumbBmps(href,widths,heights,2,ink,true);
    assert(Storage.files.empty() && decodes==0);
    ++cases;
    // The copy works but the part file cannot be opened: the cover read is closed, the copy removed.
    Storage.files.clear(); readOk=true; partOpens=false;
    epub.generateThumbBmps(href,widths,heights,2,ink,true);
    assert(Storage.files.empty() && decodes==0);
    ++cases;
    // Then a working card draws both thumbnails.
    partOpens=true;
    epub.generateThumbBmps(href,widths,heights,2,ink,true);
    assert(decodes==2 && Storage.files.size()==2);
    for (const auto& [name,data] : Storage.files) assert(!name.ends_with(".tmp") && name.find(".cover.")==std::string::npos);
    ++cases;
  }
  std::printf("GREEN %d cover copy cases: failed copy and unopened part leave no .tmp, no temporary cover, no abort\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='cover-copy-') as tmp:
    cpp = Path(tmp) / 'copy.cpp'
    cpp.write_text(stubs + paths + body + main)
    exe = Path(tmp) / 'copy'
    subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I' + str(repo / 'src'), '-I' + str(repo / 'lib/JpegToBmpConverter'),
                    str(cpp), '-o', str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
