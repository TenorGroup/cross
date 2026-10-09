"""Execute production bars with real handwritten glyphs and a recording renderer."""
import argparse
from pathlib import Path
import subprocess


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
#include "shells/ugly/UglyLogic.h"
#include "shells/ugly/fonts/ugly_22.h"
struct CrossPointSettings {
  enum { HIDE_NEVER=0, HIDE_READER=1, HIDE_ALWAYS=2 };
  enum { CLOCK_HEADER_HIDE=0, CLOCK_HEADER_TIME=1, CLOCK_HEADER_TIME_DATE=2 };
  int hideBatteryPercentage=0, clockShowInHeader=2, clockFormat=0, uglyBatteryHidden=0;
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
struct GfxRenderer {
  int screenWidth=528, screenHeight=792;
  int getScreenWidth() const { return screenWidth; }
  int getScreenHeight() const { return screenHeight; }
};
struct MappedInputManager {
  struct Labels { const char *btn1,*btn2,*btn3,*btn4; };
  Labels mapLabels(const char* back,const char* confirm,const char* up,const char* down) const {
    return {back,confirm,up,down};
  }
};
namespace ugly {
enum class Size { S22 };
enum class Mark { Back,Tick,Up,Down };
struct Hints { bool back,confirm,left,right; };
struct Draw { int textX,baseline,span; std::string value; };
std::vector<Draw> draws;
std::vector<std::pair<int,int>> digitPixels;
std::string digitValues;
const EpdFont FONT22(&ugly_22);
struct Letter { const EpdGlyph* glyph; logic::Warp warp; };
void drawWarped(const GfxRenderer&, Size, const Letter& letter, int textX, int baseline, bool) {
  const auto* glyph=letter.glyph;
  for (char digit='0';digit<='9';++digit) if (FONT22.getGlyph(digit)==glyph) digitValues+=digit;
  logic::warpGlyph(FONT22.data->bitmap+glyph->dataOffset,glyph->width,glyph->height,
                   glyph->left,glyph->top,glyph->advanceX,letter.warp,logic::NO_CLIP,
                   [&](int pixelX,int pixelY) { digitPixels.emplace_back(textX+pixelX,baseline+pixelY); });
}
int width(const GfxRenderer&, Size, const char* value) { return strlen(value)*12; }
int text(const GfxRenderer& renderer,Size size,int textX,int baseline,const char* value) {
  int span=width(renderer,size,value); draws.push_back({textX,baseline,span,value}); return span;
}
std::string fit(const GfxRenderer&,Size,const char* value,int) { return value; }
void mark(const GfxRenderer&,Mark,int centreX,int centreY) { draws.push_back({centreX-12,centreY+8,24,"mark"}); }
void stroke(const GfxRenderer&,int,int,int,int,int) {}
void polyline(const GfxRenderer&,const int (*)[2],int,int) {}
'''
if 'void batteryDigits(' in source:
    cpp += method(source, 'void batteryDigits(') + '\n'
for signature in ('void battery(', 'bool clockText(', 'void statusBar(', 'void topBar(', 'void formTopBar('):
    cpp += method(source, signature) + '\n'
cpp += r'''
}
int main() {
  GfxRenderer renderer; MappedInputManager input;
  int cases=0;
  for (int screenWidth : {480,528,792}) for (int mode : {0,1,2}) for (int level=0;level<=100;++level) for (int screen : {0,1,2}) {
    ++cases;
    renderer.screenWidth=screenWidth;
    SETTINGS.hideBatteryPercentage=mode; powerManager.level=level; ugly::draws.clear();
    ugly::digitPixels.clear(); ugly::digitValues.clear();
    if (screen==0) ugly::statusBar(renderer,input,{true,true,true,true});
    if (screen==1) ugly::topBar(renderer,"Home");
    if (screen==2) ugly::formTopBar(renderer);
    int count=0;
    for (const auto& draw : ugly::draws) if (draw.value.find('%')!=std::string::npos) {
      ++count; assert(draw.value==std::to_string(level)+"%");
      assert(draw.textX>=0 && draw.textX+draw.span<=renderer.screenWidth);
      for (const auto& other : ugly::draws) if (&other!=&draw && std::abs(other.baseline-draw.baseline)<20)
        assert(draw.textX+draw.span<=other.textX || other.textX+other.span<=draw.textX);
    }
    if (screen==0) {
      int hint=0;
      const int wide[4]={105,197,331,423}, narrow[4]={98,186,294,382};
      for (const auto& draw:ugly::draws) if (draw.value=="mark") {
        if (draw.textX+12!=(screenWidth>=528?wide:narrow)[hint++]) {
          fprintf(stderr,"RED physical hint moved screenWidth=%d mode=%d level=%d\n",screenWidth,mode,level);
          return 1;
        }
      }
      assert(hint==4);
      const std::string expected=SETTINGS.batteryPercentShown(false)?std::to_string(level):"";
      if (ugly::digitValues!=expected) {
        fprintf(stderr,"RED inside battery digits=%s expected=%s\n",ugly::digitValues.c_str(),expected.c_str());
        return 1;
      }
      int minX=999,minY=999,maxX=-1,maxY=-1;
      for (const auto& pixel:ugly::digitPixels) {
        assert(pixel.first>=18 && pixel.first<45 && pixel.second>=renderer.screenHeight-27 && pixel.second<renderer.screenHeight-13);
        minX=std::min(minX,pixel.first);maxX=std::max(maxX,pixel.first);
        minY=std::min(minY,pixel.second);maxY=std::max(maxY,pixel.second);
      }
      if (!expected.empty()) {
        assert(!ugly::digitPixels.empty());
        assert(std::abs(minX+maxX-62)<=1);
        assert(std::abs(minY+maxY-2*(renderer.screenHeight-20))<=1);
      }
    }
    if (count!=((screen!=0 && SETTINGS.batteryPercentShown(false))?1:0)) {
      fprintf(stderr,"RED screen=%d mode=%d level=%d percent draws=%d\n",screen,mode,level,count);
      return 1;
    }
  }
  SETTINGS.hideBatteryPercentage=CrossPointSettings::HIDE_READER;
  assert(!SETTINGS.batteryPercentShown(true));
  printf("GREEN %d production bar cases, real digit pixels centred inside body, fixed hint coordinates, header percent, reader contract\n",cases);
}
'''
translation_unit = args.output / 'battery.cpp'
translation_unit.write_text(cpp)
program = args.output / 'battery'
subprocess.run(['c++', '-std=c++20', '-I'+str(args.repo/'src'), '-I'+str(args.repo/'lib/EpdFont'),
                '-I'+str(args.repo/'lib/Utf8'), str(translation_unit), str(args.repo/'lib/EpdFont/EpdFont.cpp'),
                str(args.repo/'lib/Utf8/Utf8.cpp'), '-o', str(program)], check=True)
subprocess.run([str(program)], check=True)
