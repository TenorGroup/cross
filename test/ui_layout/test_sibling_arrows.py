"""Raster and spacing checks for the actual sibling-tab drawing functions."""
from pathlib import Path
import re, subprocess, tempfile
ROOT = Path(__file__).resolve().parents[2]
s = (ROOT / 'src/components/TenorMenuChrome.cpp').read_text()
def function(name):
    start=s.rfind('\n',0,s.index(name))+1
    opening=s.index('{',start); end=opening+1; depth=1
    while depth:
        depth += (s[end]=='{')-(s[end]=='}'); end+=1
    return s[start:end]
constants='\n'.join(re.findall(r'constexpr int SIBLING_[^;]+;',s))
fixture=r"""
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
constexpr int UI_10_FONT_ID=10, UI_12_FONT_ID=12, HEADER_TOP=5;
int tier=0;
namespace EpdFontFamily { enum Style { REGULAR }; }
namespace BidiUtils { enum class BidiBaseDir { AUTO }; }
struct Rect { int x,y,w,h; };
struct GfxRenderer {
 int width; mutable std::vector<Rect> text;
 mutable std::vector<std::pair<int,int>> ink;
 int getScreenWidth() const { return width; }
 int getLineHeight(int font) const { return (font==12?33:26)+tier*5; }
 int getTextWidth(int,const char* s,EpdFontFamily::Style,BidiUtils::BidiBaseDir,int) const { return std::string(s).size()*(8+tier); }
 std::string truncatedText(int,const char* s,int max,EpdFontFamily::Style,int) const {
   return std::string(s).substr(0,std::max(0,max)/(8+tier));
 }
 void drawText(int f,int x,int y,const char* s,bool,EpdFontFamily::Style st,BidiUtils::BidiBaseDir d,int t) const {
   text.push_back({x,y,getTextWidth(f,s,st,d,t),getLineHeight(f)});
 }
 void drawLine(int x,int y,int endX,int endY) const {
   int dx=std::abs(endX-x),sx=x<endX?1:-1,dy=-std::abs(endY-y),sy=y<endY?1:-1,err=dx+dy;
   for(;;) { ink.emplace_back(x,y); if(x==endX&&y==endY)break; int e=2*err;
     if(e>=dy){err+=dy;x+=sx;} if(e<=dx){err+=dx;y+=sy;}
   }
 }
};
namespace tenorchrome {
int headerHeight(){return 48+tier*5;}
int tabTop(){return HEADER_TOP+headerHeight();}
int tabHeight(){return 59+tier*5;}
void drawSiblingDestinations(const GfxRenderer&,const char*,const char*);
}
using tenorchrome::headerHeight;
using tenorchrome::tabTop;
using tenorchrome::tabHeight;
// PRODUCTION
int main() {
 int failures=0, cases=0;
 auto check=[&](bool ok,const char* label){ if(!ok){++failures;std::printf("FAIL tier=%d: %s\n",tier,label);} };
 for(tier=0;tier<3;++tier) for(int width:{320,480,528}) {
   GfxRenderer r{width, {}, {}};
   tenorchrome::drawSiblingDestinations(r,"Long previous tab title","Display and interface settings");
   check(r.text.size()==2,"both labels drawn");
   for(bool right:{false,true}) {
     int minX=width,maxX=-1,minY=800,maxY=-1,count=0;
     std::vector<std::pair<int,int>> pixels=r.ink;
     std::sort(pixels.begin(),pixels.end());pixels.erase(std::unique(pixels.begin(),pixels.end()),pixels.end());
     for(auto [x,y]:pixels) if((x>width/2)==right) {
       ++count;minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);
     }
     check(maxX-minX+1>=8&&maxY-minY+1>=11,"arrow must be visibly larger than the old 6x8");
     check(count>=22,"arrow must have a heavier stroke");
     check(minX>=0&&maxX<width&&minY>=tabTop()&&maxY<tabTop()+tabHeight(),"arrow stays in tab band");
     if(r.text.size()==2) {
       const auto t=r.text[right?1:0];
       check(right ? minX-(t.x+t.w)>=6 : t.x-(maxX+1)>=6,"arrow keeps label gap");
     }
   }
   if(r.text.size()==2) check(r.text[1].x-(r.text[0].x+r.text[0].w)>=16,"long labels stay apart");
   ++cases;
 }
 std::printf("%d layout cases, %d failures\n",cases,failures);
 return failures?1:0;
}
"""
fixture=fixture.replace('// PRODUCTION',constants+'\n'+function('void drawSiblingChevron(')+'\n'+function('void tenorchrome::drawSiblingDestinations('))
with tempfile.TemporaryDirectory(prefix='sibling-arrows-') as temp:
    src=Path(temp)/'test.cpp';exe=Path(temp)/'test';src.write_text(fixture)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-UNDEBUG',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
