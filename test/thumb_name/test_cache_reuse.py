#!/usr/bin/env python3
"""Run the production EPUB cache-hit path with counted storage/decoder boundaries."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
source = (repo / 'lib/Epub/Epub.cpp').read_text()
paths = source[source.index('std::string Epub::getThumbBmpPath()'):source.index('bool Epub::isCoverImage')]
start = source.index('void Epub::generateThumbBmps(const std::string&')
body = source[start:source.index('// One height,', start)]
stubs = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "util/ThumbName.h"
#include "TenorInk.h"
int copies=0, decodes=0, writes=0, inkDecodes=0;
struct HalFile { std::vector<unsigned char>* data=nullptr; explicit operator bool() const { return data!=nullptr; } void close() {} size_t size() const { return data?data->size():0; } };
struct Store {
  std::map<std::string,std::vector<unsigned char>> files;
  bool exists(const char* p) const { return files.count(p); }
  bool remove(const char* p) { return files.erase(p); }
  bool openFileForWrite(const char*,const std::string& p,HalFile& f) { ++writes; f.data=&files[p]; return true; }
  bool openFileForRead(const char*,const std::string& p,HalFile& f) { if(!exists(p.c_str())) return false; f.data=&files[p]; return true; }
  bool rename(const char* a,const char* b) { files[b]=files[a]; files.erase(a); return true; }
} Storage;
namespace coverink { bool restore(const std::string&) { return true; } }  // the ink backup: test/cover_ink
struct GrayThumb { explicit GrayThumb(int) {} void alsoFeed(GrayThumb*) {} bool ready() const { return true; }
  bool writeTo(HalFile& f) const { *f.data={1,2,3}; return true; }
  bool writeScaled(int,HalFile& f) const { return writeTo(f); }
};
struct FsHelpers { static bool hasJpgExtension(const std::string& s) { return s.ends_with(".jpg"); }
  static bool hasPngExtension(const std::string& s) { return s.ends_with(".png"); } };
struct JpegToBmpConverter {
  static bool jpegFileToInkBmpStream(HalFile&,HalFile& f,int,int,bool four) { ++decodes; ++inkDecodes; *f.data={static_cast<unsigned char>(four?4:1)}; return true; }
  static bool jpegFileToGrayThumb(HalFile&,GrayThumb&,int*) { ++decodes; return true; }
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile&,HalFile& f,int,int,bool=false) { ++decodes; *f.data={1}; return true; }
};
struct PngToBmpConverter { static bool pngFileToInkBmpStream(HalFile&,HalFile& f,int,int,bool four) { return JpegToBmpConverter::jpegFileToInkBmpStream(f,f,0,0,four); } static bool pngFileTo1BitBmpStreamWithSize(HalFile&,HalFile& f,int,int,bool=false) { ++decodes; *f.data={1}; return true; } };
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
  bool readItemContentsToStream(const std::string&,HalFile& f,int) const { ++copies; *f.data={9}; return true; }
  void generateThumbBmps(const std::string&,const int*,int) const;
  void generateThumbBmps(const std::string&,const int*,const int*,int,bool=false,bool=true) const;
};
'''
main = r'''
int main() {
  Epub epub;
  const int heights[]={226,450,145};
  // Existing drawable 1-bit BMP files: header, palette and packed rows survive exactly.
  for(int h:heights) {
    std::vector<unsigned char> bmp(62+((h*298/450+31)/32)*4*h,0x55);
    std::fill_n(bmp.begin(),62,0);
    bmp[0]='B'; bmp[1]='M';
    const auto field=[&](int at,int value,int bytes=4) { for(int i=0;i<bytes;++i) bmp[at+i]=(value>>(8*i))&255; };
    field(2,bmp.size()); field(10,62); field(14,40); field(18,h*298/450); field(22,-h);
    field(26,1,2); field(28,1,2); field(34,bmp.size()-62); field(46,2);
    bmp[58]=bmp[59]=bmp[60]=255;
    Storage.files[epub.cachePath+"/thumb2_"+std::to_string(h)+".bmp"]=bmp;
  }
  const auto before=Storage.files;
  std::string recent=epub.cachePath+"/thumb2_[HEIGHT].bmp";
  thumbname::moveOldEpubThumb(recent);
  if(recent!=epub.cachePath+"/thumb2_[HEIGHT].bmp") puts("FAIL valid Recent thumb2 remapped");
  epub.generateThumbBmps("images/cover.jpg",heights,3);
  std::printf("cache hit: copies=%d decodes=%d writes=%d files=%zu\n",copies,decodes,writes,Storage.files.size());
  if(recent!=epub.cachePath+"/thumb2_[HEIGHT].bmp" || copies || decodes || writes || Storage.files!=before) return 1;
  // Exercise the same real branch with missing cache, to prove the decoder counter is live.
  Storage.files.erase(epub.getThumbBmpPath(450));
  const int missing=450;
  epub.generateThumbBmps("images/cover.jpg",&missing,1);
  assert(copies==1 && decodes==1 && writes==2);
  assert(Storage.exists(epub.getThumbBmpPath(450).c_str()));
  const int widths[]={232,253}, shapeHeights[]={350,381};
  const auto oldThumb=Storage.files[epub.getThumbBmpPath(226)];
  epub.generateThumbBmps("images/cover.jpg",widths,shapeHeights,2);
  assert(copies==2 && decodes==3);
  assert(Storage.exists(epub.getThumbBmpPath(232,350).c_str()));
  assert(Storage.exists(epub.getThumbBmpPath(253,381).c_str()));
  const auto shapeCache=Storage.files;
  const int oldWrites=writes;
  epub.generateThumbBmps("images/cover.jpg",widths,shapeHeights,2);
  assert(copies==2 && decodes==3 && writes==oldWrites && Storage.files==shapeCache);
  assert(Storage.files[epub.getThumbBmpPath(226)]==oldThumb);
  const auto legacy = Storage.files;
  epub.generateThumbBmps("images/cover.jpg",widths,shapeHeights,2,true,true);
  assert(inkDecodes==2);
  const auto grayPath=tenorink::cachePath(epub.getThumbBmpPath(),232,350,true);
  assert(Storage.files[grayPath]==std::vector<unsigned char>{4});
  const int afterGrayCopies=copies,afterGrayWrites=writes;
  epub.generateThumbBmps("images/cover.jpg",widths,shapeHeights,2,true,true);
  assert(inkDecodes==2 && copies==afterGrayCopies && writes==afterGrayWrites);
  epub.generateThumbBmps("images/cover.png",widths,shapeHeights,2,true,false);
  const auto monoPath=tenorink::cachePath(epub.getThumbBmpPath(),232,350,false);
  assert(inkDecodes==4 && grayPath!=monoPath);
  assert(Storage.files[monoPath]==std::vector<unsigned char>{1});
  for(const auto& [name,data]:legacy) assert(Storage.files[name]==data);
  puts("PASS legacy bytes, exact shape, separate tenor/ink depths and warm cache reuse");
}
'''
with tempfile.TemporaryDirectory(prefix='cover-cache-') as tmp:
    cpp=Path(tmp)/'cache.cpp'
    cpp.write_text(stubs+paths+body+main)
    exe=Path(tmp)/'cache'
    subprocess.run(['c++','-std=c++20','-I'+str(repo/'src'),'-I'+str(repo/'lib/JpegToBmpConverter'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
