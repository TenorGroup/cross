"""Execute classic Ugly painter with persisted reordered tabs and pin/chevron gutters."""
from pathlib import Path
import argparse
import subprocess
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
s=(a.repo/'src/activities/reader/EpubReaderMenuActivity.cpp').read_text();start=s.index('void EpubReaderMenuActivity::paintUglyMenu');end=s.index('{',start)+1;depth=1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
painter=s[start:end]
cpp=r'''
#include <FreeInkUI.h>
#include <I18n.h>
#include <MenuCustomization.h>
#include <GfxRenderer.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
namespace fui=freeink::ui;
I18n&I18n::getInstance(){static I18n i;return i;}const char*I18n::get(StrId)const{return "caption";}
namespace menucustom {State&state(){static State s;return s;}}
namespace tenorchrome {constexpr bool kTouchShell=false;int footBackReserve(){return 76;}int contentTop(){return 38;}int tips=0;void drawTip(const GfxRenderer&,const char*,int,int){++tips;}}
struct UITheme {struct Metrics{int buttonHintsHeight=44;};static UITheme getInstance(){return {};}Metrics getMetrics()const{return {};}};
struct Run{fui::Rect box;std::string label;};std::vector<Run> runs;std::vector<fui::Rect> rings;
struct Glyph{int x,y;};std::vector<Glyph> ticks,arrows;
namespace ugly {
 enum class Size{S22};enum class Mark{Right};int width(const GfxRenderer&,Size,const char*s){return strlen(s)*10;}int ascent(Size){return 22;}
 void tick(const GfxRenderer&,int x,int y){ticks.push_back({x,y});}
 void mark(const GfxRenderer&,Mark,int x,int y){arrows.push_back({x,y});}
 void paragraph(const GfxRenderer&,Size,int,int,int,int,const char*){}
}
namespace readerugly {
 void paper(const GfxRenderer&,fui::Rect){}
 void selected(const GfxRenderer&,fui::Rect box){rings.push_back(box);}
 void text(const GfxRenderer&,fui::Rect box,const char*s,fui::TextAlign=fui::TextAlign::Left){runs.push_back({box,s?s:""});}
}
struct EpubReaderMenuActivity {
 GfxRenderer renderer;std::string name="EpubReaderMenu";
 fui::Rect skinTab_{0,60,528,50},skinProgress_{0,112,528,40},skinHint_{};
 int currentPage=12,totalPages=40,bookProgressPercent=51,rowCount=1,shown=4,first=0;bool pinned=true;
 fui::ListItem menuRowItems[1];
 struct App{fui::Rect publishedRect(int action,int)const{return action==1?fui::Rect{16,160,496,60}:fui::Rect{};}}app;
 static constexpr int ACTION_TAB=2,ACTION_ROW=1;
 struct Nav{int top=0;}nav;Nav&activeNav(){return nav;}
 int tabCount()const{return 4;}int tabWindowCount()const{return shown;}int tabWindowStart()const{return first;}
 const char*tabLabel(int i)const{static const char*labels[]={"tab0","tab1","tab2","tab3"};return labels[i];}
 int activeTab()const{return 3;}int ringPos()const{return 1;}bool rowIsPinned(int)const{return pinned;}
 void paintUglyMenu();
};
'''+painter+r'''
int main(){
 menucustom::state().order[2]={2,0,3,1,4,5,6,7};
 for(int width:{528,792})for(int shown:{4,2})for(bool pinned:{false,true})for(bool opens:{false,true}){
  runs.clear();ticks.clear();arrows.clear();rings.clear();EpubReaderMenuActivity menu;
  menu.renderer.width=width;menu.renderer.height=width==528?792:528;menu.skinTab_.width=width;
  menu.shown=shown;menu.first=shown==4?0:1;menu.pinned=pinned;
  menu.skinHint_={16,700,496,52};int tipsBefore=tenorchrome::tips;
  if(shown<4){menu.skinTab_.x=14;menu.skinTab_.width-=28;}
  menu.menuRowItems[0].label="Long menu row label";menu.menuRowItems[0].value="Long selected setting value";menu.menuRowItems[0].opensNext=opens;
  menu.paintUglyMenu();assert(tenorchrome::tips==tipsBefore+1);std::vector<Run> tabRuns;
  for(const auto&run:runs)if(run.label.rfind("tab",0)==0)tabRuns.push_back(run);
  assert(tabRuns.size()==static_cast<size_t>(shown));
  for(int i=0;i<shown;++i){int id=menucustom::idAt(2,menu.first+i,4);assert(tabRuns[i].label==menu.tabLabel(id));
   int inset=shown<4?14:0;int first=56+inset,last=width-57-inset;int cx=shown>1?first+(2*(last-first)*i+(shown-1))/(2*(shown-1)):width/2;
   assert(tabRuns[i].box.x+tabRuns[i].box.width/2==cx);}
  int tabRings=0;for(auto ring:rings)if(ring.y==menu.skinTab_.y){++tabRings;
   for(const auto&run:tabRuns)if(run.label=="tab3")assert(ring.x+ring.width/2==run.box.x+run.box.width/2);
  }assert(tabRings==1);
  assert(ticks.size()==static_cast<size_t>(pinned)&&arrows.size()==static_cast<size_t>(opens));
  if(pinned&&opens)assert(ticks[0].x+22<arrows[0].x-10);
  fui::Rect label{},value{};
  for(const auto&run:runs)if(run.label==menu.menuRowItems[0].label||run.label==menu.menuRowItems[0].value){
   if(pinned)assert(run.box.right()<ticks[0].x-12);
   if(opens)assert(run.box.right()<arrows[0].x-10);
   if(run.label==menu.menuRowItems[0].label)label=run.box;else value=run.box;
  }
  assert(label.right()+12==value.x);
 }
 puts("GREEN 16 classic production painter cases: 2 X3 rotations, persisted reordered tab IDs/window/centres, separate pinned tick and chevron gutters");
}
'''
(a.output/'classic.cpp').write_text(cpp);sdk=a.repo/'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-I'+str(a.repo/'test/ugly_reader/stubs'),'-I'+str(a.repo/'lib/I18n'),'-I'+str(a.repo/'src'),'-I'+str(sdk/'include'),str(a.output/'classic.cpp'),str(sdk/'src/FreeInkUI.cpp'),'-o',str(a.output/'classic')],check=True)
subprocess.run([str(a.output/'classic')],check=True)
