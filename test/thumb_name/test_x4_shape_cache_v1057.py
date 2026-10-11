#!/usr/bin/env python3
"""Run the production Home bitmap dispatch against counted files and drawing calls."""
from pathlib import Path
import argparse, subprocess, tempfile

p=argparse.ArgumentParser()
p.add_argument('--source-root', type=Path)
p.add_argument('--mutation', action='store_true')
a=p.parse_args()
root=a.source_root or Path(__file__).resolve().parents[2]
home=(root/'src/activities/home/HomeActivity.cpp').read_text()
start=home.index('  bool image = coverKept;')
body=home[start:home.index('\n  // A plain frame waits for the idle pass',start)]
if a.mutation:
    body=body.replace('touchCoverPath(book.coverBmpPath, cardThumbWidth, height)', 'UITheme::getCoverThumbPath(book.coverBmpPath, 450)')
cpp=r'''
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <utility>
#include <cstdio>
#include "TenorInk.h"
namespace tenorchrome { bool kTouchShell=true; }
bool inkX3=false, gray=true;
bool homeInkEnabled(){return tenorchrome::kTouchShell || inkX3;}
struct HalFile { int w=0,h=0; };
struct Store {
 std::map<std::string,std::pair<int,int>> files;
 std::vector<std::string> opened;
 bool openFileForRead(const char*,const std::string& path,HalFile& f){
  opened.push_back(path);auto it=files.find(path);if(it==files.end())return false;
  f.w=it->second.first;f.h=it->second.second;return true;
 }
} Storage;
namespace coverink { bool restore(const std::string&) { return true; } }  // the ink backup: test/cover_ink
enum class BmpReaderError { Ok };
struct Bitmap { HalFile f;Bitmap(HalFile file):f(file){} BmpReaderError parseHeaders(){return BmpReaderError::Ok;}
 int getWidth(){return f.w;}int getHeight(){return f.h;} };
struct UITheme { struct Metrics {int homeCoverHeight=226;};static UITheme getInstance(){return {};}
 Metrics getMetrics(){return {};}
 static std::string getCoverThumbPath(std::string,int h){return "thumb2_"+std::to_string(h)+".bmp";}
 static std::string getCoverThumbPath(std::string,int w,int h){return "thumb2_"+std::to_string(w)+"x"+std::to_string(h)+".bmp";}
};
std::string touchCoverPath(const std::string& cover,int w,int h){return tenorink::cachePath(cover,w,h,gray);}
struct Renderer {int exact=0,scaled=0;
 bool drawBitmap(Bitmap&,int,int,int,int){++exact;return true;}
 bool drawBitmapCover(Bitmap&,int,int,int,int){++scaled;return true;}
} renderer;
constexpr int HOME_CARD_COVER_H=450;
bool draw(){
 bool coverKept=false;int cardFileThumb=0,coverHeight=0,cardThumbWidth=232,cardThumbHeight=350;
 struct {std::string coverBmpPath="thumb2_[HEIGHT].bmp";} book;
 struct {int coverX=24,coverY=52,coverW=232,coverH=350;} card;
 @BODY@
 return image;
}
void reset(){Storage.files.clear();Storage.opened.clear();renderer={};}
int main(){
 reset();Storage.files["ink2_4_232x350.bmp"]={232,350};Storage.files["thumb2_450.bmp"]={298,450};
 assert(draw());assert(renderer.exact==1&&renderer.scaled==0);
 assert(Storage.opened==std::vector<std::string>{"ink2_4_232x350.bmp"});
 reset();Storage.files["ink2_4_232x350.bmp"]={233,350};assert(!draw());assert(!renderer.exact&&!renderer.scaled);
 reset();Storage.files["thumb2_450.bmp"]={298,450};assert(!draw());assert(!renderer.exact&&!renderer.scaled);
 assert(Storage.opened==std::vector<std::string>{"ink2_4_232x350.bmp"});
 reset();tenorchrome::kTouchShell=false;Storage.files["thumb2_450.bmp"]={298,450};
 assert(draw());assert(renderer.scaled==1&&renderer.exact==0);
 reset();inkX3=true;Storage.files["ink2_4_232x350.bmp"]={232,350};
 assert(draw());assert(renderer.exact==1&&renderer.scaled==0);
 reset();gray=false;Storage.files["ink2_1_232x350.bmp"]={232,350};
 assert(draw());assert(renderer.exact==1&&renderer.scaled==0);
 assert(Storage.opened==std::vector<std::string>{"ink2_1_232x350.bmp"});
 puts("GREEN exact dimensions, X3 gray and mono caches, legacy button fallback");
}
'''.replace('@BODY@',body)
with tempfile.TemporaryDirectory(prefix='home-shape-') as tmp:
    src=Path(tmp)/'dispatch.cpp';src.write_text(cpp)
    exe=Path(tmp)/'dispatch'
    subprocess.run(['c++','-std=c++20','-UNDEBUG','-I'+str(root/'lib/JpegToBmpConverter'),str(src),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)])
if a.mutation:
    assert result.returncode != 0, 'legacy 450 mutation survived'
    print('GREEN: X4 thumb2_450 mutation rejected (RED assertion)')
else:
    raise SystemExit(result.returncode)
