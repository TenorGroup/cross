"""Execute the production slot painter against observable text/icon bounds."""
from pathlib import Path
import argparse
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument('--mutation', action='store_true')
p.add_argument('--check', choices=['tail', 'layout', 'all'], default='all')
p.add_argument('--preview', action='store_true')
a = p.parse_args()
repo = a.repo

def method(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

header = (repo / 'src/CrossPointSettings.h').read_text()
enums = method(header, 'enum READER_STATUS_SLOT') + ';\n'
enums += method(header, 'enum STATUS_BAR_TITLE') + ';\n'
spec = method(header, 'struct StatusBarSpec') + ';\n'
body = method((repo / 'src/components/TenorMenuChrome.cpp').read_text(),
              'void tenorchrome::drawReaderSlots(')
metric_body = method((repo / 'src/components/TenorMenuChrome.cpp').read_text(),
                     'tenorchrome::BatteryInkBounds tenorchrome::batteryInkBounds(')
if a.mutation:
    body = body.replace('case S::STATUS_SLOT_NONE: continue;', 'case S::STATUS_SLOT_NONE: break;')

cpp = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "Utf8.h"
#include "activities/reader/ReaderStatusLayout.h"
constexpr int SMALL_FONT_ID = 0;
struct EpdFontFamily {enum Style {REGULAR};};
struct CrossPointSettings {
 enum {STATUS_BAR_CLOCK_HIDE=0,HIDE_PROGRESS=2,XTC_STATUS_BAR_HIDE=0};
''' + enums + spec + r'''
 StatusBarSpec spec;
 int clockFormat = 0;
 bool percent = true;
 bool hidden = false;
 StatusBarSpec statusBarSpec() const {return spec;}
 bool batteryPercentShown(bool) const {return percent;}
 bool readerStatusBarHidden() const {return hidden;}
} SETTINGS;
struct Ink {int x,y,w,h; std::string text;};
std::vector<Ink> ink;
bool rough = false;
struct GfxRenderer {
 int w = 480, h = 800, advance = 8, line = 21;
 int getScreenWidth() const {return w;}
 int getScreenHeight() const {return h;}
 int getTextWidth(int,const char* text) const {
   int count=0;
   for(const unsigned char* cursor=reinterpret_cast<const unsigned char*>(text);*cursor;++cursor)
     if((*cursor&0xc0)!=0x80)++count;
   return count*advance;
 }
 int getTextInkTop(int,const char*,int) const {return 5;}
 int getTextInkBottom(int,const char*,int) const {return line-4;}
 std::string truncatedText(int,const char* text,int width) const {
   std::string out=text;
   if(width<=0)return "";
   if(getTextWidth(0,text)<=width)return out;
   while(!out.empty()&&getTextWidth(0,(out+"…").c_str())>=width)utf8RemoveLastChar(out);
   return out+"…";
 }
 void drawText(int,int x,int y,const char* text) const {ink.push_back({x,y,getTextWidth(0,text),line,text});}
 void drawRect(int x,int y,int w,int h,int,bool) const {ink.push_back({x,y,w,h,"icon"});}
 void fillRect(int x,int y,int w,int h) const {ink.push_back({x,y,w,h,"fill"});}
};
namespace shell {bool uglyParts(){return rough;}}
namespace ugly {
 enum class Size{S22};
 int width(const GfxRenderer& r,Size,const char* text){return r.getTextWidth(0,text);}
 std::string fit(const GfxRenderer& r,Size,const char* text,int room){return r.truncatedText(0,text,room);}
 void text(const GfxRenderer& r,Size,int x,int y,const char* value){r.drawText(0,x,y-18,value);}
 void battery(const GfxRenderer&,int x,int y,int){ink.push_back({x,y-8,44,18,"icon"});}
}
namespace clockstatus {bool valid=true;bool hasValidTime(){return valid;}}
struct Clock {bool formatTime(char* value,size_t size,bool){snprintf(value,size,"12:34");return true;}}halClock;
struct Power {int percentage=100;int getDisplayedBatteryPercentage(){return percentage;}}powerManager;
struct Gpio {bool charging=false;bool isUsbConnected(){return charging;}}gpio;
namespace inlineSymbols {
 enum class Shape {Star};
 void drawShape(const GfxRenderer&,Shape,int,int,int,bool){}
 int markTopOnCapitals(const GfxRenderer&,int,int y,int height){return y + (height == 14 ? 4 : 6);}
}
void drawChargingBolt(const GfxRenderer&,int,int,int,int,bool){}
namespace tenorchrome {
 struct BatteryInkBounds {int top;int height;};
 BatteryInkBounds batteryInkBounds(const GfxRenderer&,int,int);
 int statusTextY(int height,bool){return height-40;}
 void drawReaderSlots(const GfxRenderer&,const char*,int,int,float,bool,bool,int64_t,int64_t,const char* = nullptr);
}
using namespace tenorchrome;
''' + metric_body + '\n' + body + r'''
void chapterRegression(bool tail){
 using S=CrossPointSettings;
 const std::string title="Chương 12: Gã Lá Nhỏ Và Những Người Bạn ở cuối con đường rất dài";
 int cases=0;
 for(int screen:{480,528})for(int size:{8,10,12})for(bool ugly:{false,true})
  for(int left=0;left<S::STATUS_SLOT_COUNT;++left)for(int right=0;right<S::STATUS_SLOT_COUNT;++right)
   for(bool percent:{false,true}){
    rough=ugly;GfxRenderer renderer;renderer.w=screen;renderer.advance=size;
    SETTINGS.spec={};SETTINGS.spec.slotsEnabled=true;SETTINGS.spec.topTitleMode=S::HIDE_TITLE;
    SETTINGS.spec.slots[0]=left;SETTINGS.spec.slots[1]=S::STATUS_SLOT_CHAPTER_NAME;SETTINGS.spec.slots[2]=right;
    SETTINGS.percent=percent;ink.clear();clockstatus::valid=true;
    drawReaderSlots(renderer,"",9999,99999,11.0f,true,false,UINT32_MAX,UINT32_MAX,title.c_str());
    const Ink* chapter=nullptr;
    int leftEnd=12,rightStart=screen-12;
    for(const auto& painted:ink){
      if(painted.text.starts_with("Chương")){chapter=&painted;continue;}
      if(painted.x<screen/2)leftEnd=std::max(leftEnd,painted.x+painted.w);
      else rightStart=std::min(rightStart,painted.x);
    }
    assert(chapter);
    if(tail){
      const auto& label=chapter->text;
      assert(label.ends_with("…"));
      assert(label.find("…")==label.size()-3);
      const size_t prefixBytes=label.size()-3;
      assert(title.compare(0,prefixBytes,label,0,prefixBytes)==0);
      assert((static_cast<unsigned char>(title[prefixBytes])&0xc0)!=0x80);
    }else{
      const int side=std::max(leftEnd-12,screen-12-rightStart);
      const int room=std::max(0,screen-24-2*side-16);
      const auto expected=renderer.truncatedText(0,title.c_str(),room);
      assert(chapter->text==expected);
      assert(chapter->x-leftEnd>=8&&rightStart-(chapter->x+chapter->w)>=8);
      assert(std::abs(chapter->x*2+chapter->w-screen)<=1);
      if(left==S::STATUS_SLOT_NONE&&right==S::STATUS_SLOT_NONE)
        assert(chapter->w>readerstatus::cell(screen,1).width);
    }
    ++cases;
   }
 SETTINGS.percent=true;
 printf("chapter_%s:GREEN (%d production painter cases, UTF-8, cross/ugly)\n",tail?"tail":"layout",cases);
}
int main(int argc,char** argv){
 using S=CrossPointSettings;
 if(argc>1&&!strcmp(argv[1],"preview")){
   for(int screen:{528,480})for(bool ugly:{false,true}){
     GfxRenderer renderer;renderer.w=screen;renderer.advance=12;rough=ugly;powerManager.percentage=86;
     SETTINGS.spec={};SETTINGS.spec.slotsEnabled=true;SETTINGS.spec.topTitleMode=S::HIDE_TITLE;
     SETTINGS.spec.slots[0]=S::STATUS_SLOT_BATTERY;SETTINGS.spec.slots[1]=S::STATUS_SLOT_CHAPTER_NAME;
     SETTINGS.spec.slots[2]=S::STATUS_SLOT_BOOK_PERCENT;ink.clear();
     drawReaderSlots(renderer,"",1,10,11.0f,false,false,0,0,"Chuong 12: Ga La Nho Va Nhung Nguoi Ban");
     for(const auto& painted:ink)
       printf("%d\t%d\t%d\t%d\t%s\n",screen,ugly,painted.x,painted.w,painted.text.c_str());
   }
   return 0;
 }
 if(argc>1&&strcmp(argv[1],"all")){
   chapterRegression(!strcmp(argv[1],"tail"));return 0;
 }
 chapterRegression(true);chapterRegression(false);
 int cases=0;
 GfxRenderer hiddenRenderer;
 SETTINGS.spec={}; SETTINGS.spec.slotsEnabled=true; SETTINGS.spec.topTitleMode=S::HIDE_TITLE;
 SETTINGS.hidden=true; ink.clear();
 drawReaderSlots(hiddenRenderer,"Hidden",1,1,1.0f,false,true,60,60);
 assert(ink.empty());
 bool hiddenGuard = ink.empty();
 SETTINGS.hidden=false;
 for(int screen:{480,528,800,920})for(int size:{8,10,12})for(bool ugly:{false,true}){
   rough=ugly;GfxRenderer r;r.w=screen;r.advance=size;r.line=21+(size-8)*2;
   for(int top:{S::BOOK_TITLE,S::CHAPTER_TITLE,S::HIDE_TITLE})for(int slot=0;slot<3;++slot)
    for(int item=0;item<S::STATUS_SLOT_COUNT;++item)for(bool known:{false,true}){
     SETTINGS.spec={};SETTINGS.spec.slotsEnabled=true;SETTINGS.spec.topTitleMode=top;
     SETTINGS.spec.slots[slot]=item;ink.clear();clockstatus::valid=known;
     drawReaderSlots(r,"A long book or chapter title that reaches the right screen edge repeatedly",5,known?25:0,
                     known?43.0f:-1,false,true,known?5400:-1,known?25200:-1,
                     known?"Mở\nđầu một chương có tên dài để kiểm tra cắt giữa UTF-8 chapter end":nullptr);
     const auto lane=item==S::STATUS_SLOT_CHAPTER_NAME&&slot==1
       ? readerstatus::Cell{20,screen-40} : readerstatus::cell(screen,slot);
     int bottom=0,header=0;
     for(const auto& painted:ink){
       assert(painted.x>=0&&painted.x+painted.w<=screen);
       assert(painted.y>=0&&painted.y+painted.h<=r.h);
       if(painted.y<100){++header;assert(top!=S::HIDE_TITLE);}
       else{++bottom;assert(painted.x>=lane.x&&painted.x+painted.w<=lane.x+lane.width);}
     }
     assert(header==(top==S::HIDE_TITLE?0:1));
     const bool visible=item!=S::STATUS_SLOT_NONE&&(item!=S::STATUS_SLOT_CHAPTER_NAME||slot==1);
     assert((bottom>0)==visible);
     if(!known&&visible&&item!=S::STATUS_SLOT_BATTERY){
       assert(bottom==1&&ink.back().text=="-");
     }
     if(known&&item==S::STATUS_SLOT_CHAPTER_NAME&&slot==1){
       const auto& label=ink.back().text;
       const char* name="Mở đầu một chương có tên dài để kiểm tra cắt giữa UTF-8 chapter end";
       assert(bottom==1&&label.find('\n')==std::string::npos);
       assert(label==r.truncatedText(0,name,lane.width));
     }
     ++cases;
   }
 }
 printf("reader_slots_render:GREEN (%d production painter cases, 3 sizes, normal/rough, known/missing, hidden_guard=%s)\n",
        cases, hiddenGuard ? "GREEN" : "RED");
}
'''
with tempfile.TemporaryDirectory() as folder:
    source = Path(folder) / 'render.cpp'
    binary = Path(folder) / 'render'
    source.write_text(cpp)
    subprocess.run(['c++', '-std=c++20', '-I', str(repo / 'src'), '-I', str(repo / 'lib/Utf8'),
                    str(source), str(repo / 'lib/Utf8/Utf8.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary), 'preview' if a.preview else a.check], check=True)
