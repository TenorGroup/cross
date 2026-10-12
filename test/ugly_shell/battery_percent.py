"""Execute production bars with real handwritten glyphs and a recording renderer."""
import argparse
from pathlib import Path
import subprocess
import re


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = (args.repo / 'src/shells/ugly/UglyInk.cpp').read_text()
settings = (args.repo / 'src/CrossPointSettings.h').read_text()
cpp = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <EpdFont.h>
#include "shells/ugly/UglyIcons.h"
#include "InlineSymbolBitmaps.h"
#include "shells/ugly/UglyLogic.h"
#include "shells/ugly/fonts/ugly_22.h"
struct CrossPointSettings {
  enum { HIDE_NEVER=0, HIDE_READER=1, HIDE_ALWAYS=2 };
  enum { CLOCK_HEADER_HIDE=0, CLOCK_HEADER_TIME=1, CLOCK_HEADER_TIME_DATE=2 };
  enum { STATUS_BAR_CLOCK_RIGHT=1, STATUS_BAR_CLOCK_LEFT=2 };
  int hideBatteryPercentage=0, clockShowInHeader=2, clockFormat=0, uglyBatteryHidden=0;
  int statusBarClock=STATUS_BAR_CLOCK_RIGHT;
  bool globalStatusBarHidden() const { return false; }
''' + method(settings, 'bool batteryPercentShown(') + r'''
} SETTINGS;
namespace clockstatus { bool hasValidTime() { return true; } }
struct Clock {
  bool formatTime(char* out, size_t size, bool) { snprintf(out,size,"23:59"); return true; }
  bool getDateTime(uint16_t& year,uint8_t& month,uint8_t& day,uint8_t&,uint8_t&) {
    year=2026; month=10; day=31; return true;
  }
} halClock;
struct Power { int level=87; int getDisplayedBatteryPercentage() { return level; } } powerManager;
std::vector<std::pair<int,int>> pixels;
struct GfxRenderer {
  int screenWidth=528, screenHeight=792;
  int getScreenWidth() const { return screenWidth; }
  int getScreenHeight() const { return screenHeight; }
  void fillRect(int,int,int,int,bool) const {}
  void drawPixel(int x,int y) const { pixels.emplace_back(x,y); }
  void drawLine(int,int,int,int,bool) const { assert(false); }
};
struct MappedInputManager {
  struct Labels { const char *btn1,*btn2,*btn3,*btn4; };
  Labels mapLabels(const char* back,const char* confirm,const char* up,const char* down) const {
    return {back,confirm,up,down};
  }
};
namespace ugly {
enum class Size { S22 };
enum class Mark : uint8_t { Left,Right,Up,Down,Tick,Back };
struct Hints { bool back,confirm,left,right; };
struct Box { int x0=9999,y0=9999,x1=-1,y1=-1; };
Box bounds(size_t first) {
  Box box;
  for (size_t i=first;i<pixels.size();++i) {
    const auto [x,y]=pixels[i];
    box.x0=std::min(box.x0,x); box.x1=std::max(box.x1,x+1);
    box.y0=std::min(box.y0,y); box.y1=std::max(box.y1,y+1);
  }
  return box;
}
struct Draw { Box box; std::string value; };
std::vector<Draw> draws;
const EpdFont FONT22(&ugly_22);
struct Letter { const EpdGlyph* glyph; logic::Warp warp; };
void drawWarped(const GfxRenderer& r, Size, const Letter& letter, int x, int baseline, bool) {
  const auto* g=letter.glyph;
  logic::warpGlyph(FONT22.data->bitmap+g->dataOffset,g->width,g->height,g->left,g->top,
                   g->advanceX,letter.warp,logic::NO_CLIP,
                   [&](int px,int py) { r.drawPixel(x+px,baseline+py); });
}
int width(const GfxRenderer&, Size, const char* value) {
  int span=0;
  for (const char* p=value;*p;++p) span+=fp4::toPixel(FONT22.getGlyph(*p)->advanceX);
  return span;
}
int text(const GfxRenderer& r,Size size,int x,int baseline,const char* value) {
  const size_t first=pixels.size();
  int cursor=x;
  for (const char* p=value;*p;++p) {
    const auto* g=FONT22.getGlyph(*p);
    drawWarped(r,size,{g,logic::NO_WARP},cursor,baseline,true);
    cursor+=fp4::toPixel(g->advanceX);
  }
  if (*value) draws.push_back({bounds(first),value});
  return cursor-x;
}
std::string fit(const GfxRenderer&,Size,const char* value,int) { return value; }
uint32_t frameSeed=0;
uint32_t iconFrameSeed() { return frameSeed; }
'''
cpp = cpp.replace('namespace ugly {', '''namespace tenorchrome { inline bool buttonHintsShown() { return true; } }
namespace buttonSymbols {
int labelId(const char* value) {
  return static_cast<unsigned char>(value[0])==0xEE && static_cast<unsigned char>(value[1])==0x84
      ? static_cast<unsigned char>(value[2])-0x80 : -1;
}
}
namespace ugly {''', 1)
for signature in ('void dot(', 'void stroke(const GfxRenderer& r, int x0, int y0, const int x1',
                  'void penStroke(', 'void polyline('):
    cpp += method(source, signature) + '\n'
icon_source = (args.repo / 'src/shells/ugly/UglyIconInk.cpp').read_text()
cpp += method(icon_source, 'void icon(').replace('void icon(', 'void drawIcon(', 1) + '\n'
cpp += r'''
size_t iconPixels=0;
void icon(const GfxRenderer& r,icons::Id id,icons::Rect box,uint32_t seed) {
  const size_t first=pixels.size();
  drawIcon(r,id,box,seed,true);
  iconPixels+=pixels.size()-first;
  const char* name=id==icons::Id::Battery?"battery":id==icons::Id::Tick?"tick":"mark";
  draws.push_back({bounds(first),name});
}
'''
cpp += r'''
}
namespace inlineSymbols {
enum class Shape { Select };
void drawShape(const GfxRenderer& r, Shape, int x, int y, int size, bool) {
  const auto& glyph=size<=14?inlineSymbolBitmaps::kSelectNet14:inlineSymbolBitmaps::kSelectNet18;
  const size_t first=pixels.size();
  for (int row=0;row<glyph.height;++row) for (int col=0;col<glyph.width;++col)
    if (glyph.rows[row] & (1u << (glyph.width-col-1))) r.drawPixel(x+col-glyph.width/2,y+row-glyph.height/2);
  ugly::draws.push_back({ugly::bounds(first),"enter"});
}
}
namespace ugly {
'''
cpp += method(source, 'void mark(') + '\n'
if 'void batteryDigits(' in source:
    cpp += method(source, 'void batteryDigits(') + '\n'
for signature in ('void battery(', 'bool clockText('):
    cpp += method(source, signature) + '\n'
for match in re.finditer(r'\nvoid statusBar\(', source):
    cpp += method(source[match.start():], 'void statusBar(') + '\n'
for signature in ('void topBar(', 'void formTopBar('):
    cpp += method(source, signature) + '\n'
cpp += r'''
}
int main() {
  GfxRenderer renderer; MappedInputManager input;
  int cases=0, minGap=9999;
  for (uint32_t seed : {0u,1u,5u,42u,12345u}) for (int screenWidth : {480,528,792}) for (int side : {1,2})
  for (int mode : {0,1,2}) for (int level=0;level<=100;++level) for (int screen : {0,1,2}) {
    ++cases; ugly::frameSeed=seed;
    renderer.screenWidth=screenWidth;
    SETTINGS.statusBarClock=side;
    SETTINGS.hideBatteryPercentage=mode; powerManager.level=level;
    ugly::draws.clear(); pixels.clear(); ugly::iconPixels=0;
    auto check=[&](bool ok,const char* reason) {
      if (!ok) {
        fprintf(stderr,"RED %s width=%d side=%d mode=%d level=%d screen=%d seed=%u\n",
                reason,screenWidth,side,mode,level,screen,seed);
        std::exit(1);
      }
    };
    if (screen==0) ugly::statusBar(renderer,input,{true,true,true,true});
    if (screen==1) ugly::topBar(renderer,"Home");
    if (screen==2) ugly::formTopBar(renderer);
    const bool shown=SETTINGS.batteryPercentShown(false);
    int count=0, hints=0, ticks=0;
    const ugly::Draw* battery=nullptr;
    const ugly::Draw* percent=nullptr;
    for (const auto& draw : ugly::draws) {
      const auto b=draw.box;
      check(b.x0>=0 && b.x1<=screenWidth && b.y0>=0 && b.y1<=renderer.screenHeight,"ink outside screen");
      if (draw.value=="battery") battery=&draw;
      if (draw.value=="mark" || draw.value=="tick") ++hints;
      if (draw.value=="tick") ++ticks;
      if (draw.value.find('%')!=std::string::npos) {
        ++count; percent=&draw;
        check(draw.value==std::to_string(level)+"%","wrong percent value");
        for (const auto& other : ugly::draws) {
          if (&other==&draw || other.box.y1<=b.y0 || b.y1<=other.box.y0) continue;
          const int gap=std::max(other.box.x0-b.x1,b.x0-other.box.x1);
          check(gap>=0,"percent overlaps same-row ink");
          if (screen==0) {
            minGap=std::min(minGap,gap);
            if (gap<6) fprintf(stderr,"N%%=[%d,%d) %s=[%d,%d) gap=%d px\n",b.x0,b.x1,
                               other.value.c_str(),other.box.x0,other.box.x1,gap);
            check(gap>=6,"percent clearance below 6 px");
          }
        }
      }
    }
    check(count==(shown?1:0),"missing or unexpected N% text");
    check(battery!=nullptr,"battery missing");
    if (screen==0) {
      check(hints==4 && ticks==1,"four pen hints with one select tick required");
      if (shown) {
        check(side==2 ? percent->box.x1<=battery->box.x0 : percent->box.x0>=battery->box.x1,
              "percent must stand beside battery on inner side");
      }
      for (const auto& draw : ugly::draws) {
        if (draw.value!="mark" && draw.value!="tick") continue;
        const int gap=std::max(draw.box.x0-battery->box.x1,battery->box.x0-draw.box.x1);
        check(gap>=6,"battery clearance from key ink below 6 px");
      }
      // Draw the battery alone so text and key strokes cannot count as its fill.
      pixels.clear(); ugly::iconPixels=0;
      ugly::battery(renderer,side==2?screenWidth-56:14,renderer.screenHeight-20,level,shown);
      const size_t fill=pixels.size()-ugly::iconPixels;
      check(shown ? fill==0 : (level<4 ? fill==0 : fill>0),"empty percent body or hidden-mode level strokes");
    }
  }
  SETTINGS.hideBatteryPercentage=CrossPointSettings::HIDE_READER;
  assert(!SETTINGS.batteryPercentShown(true));
  printf("GREEN %d bar cases, N%% beside empty battery, minimum gap %d px, pen tick, level strokes, headers, reader mode\n",cases,minGap);
}

'''
translation_unit = args.output / 'battery.cpp'
translation_unit.write_text(cpp)
program = args.output / 'battery'
subprocess.run(['c++', '-std=c++20', '-I'+str(args.repo/'src'), '-I'+str(args.repo/'lib/EpdFont'),
                '-I'+str(args.repo/'lib/Utf8'), '-I'+str(args.repo/'lib/GfxRenderer'), str(translation_unit), str(args.repo/'lib/EpdFont/EpdFont.cpp'),
                str(args.repo/'lib/Utf8/Utf8.cpp'), '-o', str(program)], check=True)
subprocess.run([str(program)], check=True)
