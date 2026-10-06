"""Execute the production shared paint guards, retaining navigation cleanup and tab hits."""
from pathlib import Path
import argparse
import subprocess
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def method(s,key):
 start=s.index(key);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
lists=(a.repo/'src/activities/UiListActivity.cpp').read_text();tabs=(a.repo/'src/activities/UiTabListActivity.cpp').read_text()
cpp=r'''
#include <FreeInkUIGfxRenderer.h>
#include <cassert>
#include <cstdio>
namespace fui=freeink::ui;
int restores=0;
void restorePinnedRows(){++restores;}
namespace tenorchrome {
 bool kTouchShell=true;bool enabled(){return true;}
 void drawMoreBelowChevron(const GfxRenderer&r,int){++r.draws;}
 void drawTip(const GfxRenderer&r,const char*,int){++r.draws;}
 void drawPillRing(const GfxRenderer&r,int,int,int,int,int,bool){++r.draws;}
 constexpr int BAR_TAB_W=84,BAR_TAB_INSET=6;
 void drawBarTab(const GfxRenderer&r,int,int,int,const uint8_t*,int,int,bool){++r.draws;}
}
struct UiAppHost {int layouts=0;void renderUi(){++layouts;}};
struct UiListActivity:UiAppHost {
 GfxRenderer renderer;fui::GfxRendererTarget uiTarget{renderer};
 bool tabBandDrawn=true,rowsFramed=true;int favoriteHintY=1,pageAnchorRow=7;
 struct Nav {int top=0;bool rebuildNeeded=false;int pageRowsFor(int)const{return 4;}int pageRows()const{return 4;}} nav;
 Nav&activeNav(){return nav;}int listCount(){return 10;}int fadeKeepRows(){return 1;}
 struct RowFrameStyle{};RowFrameStyle rowFrameStyle()const{return {};}
 void drawRowFrame(const RowFrameStyle& ={}){++renderer.draws;}void drawPageHints(){++renderer.draws;}
 const char*favoriteHintText(){return "Help";}int favoriteHintLinesAbove(){return 1;}
 void renderUi();
};
struct Screen {fui::Frame<8>& f;fui::DrawTarget& target(){return f.target();}fui::Frame<8>&frame(){return f;}};
struct UiTabListActivity {
 using UiScreen=Screen;static constexpr fui::ActionId ACTION_TAB=1;
 GfxRenderer renderer;fui::GfxRendererTarget uiTarget{renderer};
 void drawGreyIcon(const GfxRenderer&r,fui::BitmapRef,int,int){++r.draws;}
 void veThanhTheTenor(UiScreen&,const fui::Rect&,const fui::TabItem*,int,const fui::TextStyle&);
};
'''+method(lists,'void UiListActivity::renderUi()')+'\n'+method(tabs,'void UiTabListActivity::veThanhTheTenor')+r'''
int main(){
 for(bool enabled:{true,false}){
  UiListActivity list;list.uiTarget.setPaintingEnabled(enabled);list.renderUi();
  assert(list.layouts==1);assert(list.pageAnchorRow==-1);assert(list.nav.top==6);assert(list.nav.rebuildNeeded);
  assert(list.favoriteHintY==-1&&!list.rowsFramed&&!list.tabBandDrawn);
  assert(enabled?list.renderer.draws>0:list.renderer.draws==0);
 }
 assert(restores==2);
 for(bool icons:{false,true})for(bool touch:{false,true}){
  tenorchrome::kTouchShell=touch;UiTabListActivity tab;
  fui::DeviceContext device=tab.uiTarget.deviceContext();fui::InputSnapshot input;
  fui::InteractionBuffer<8> cross,ugly;
  fui::Frame<8> cf(tab.uiTarget,device,input,cross),uf(tab.uiTarget,device,input,ugly);Screen cs{cf},us{uf};
  static const uint8_t pixels[]={255};fui::TabItem items[4];
  for(int i=0;i<4;++i){items[i].label="Reader tab";items[i].value=i;items[i].selected=i==1;
   if(icons){items[i].icon.data=pixels;items[i].icon.width=8;items[i].icon.height=1;items[i].icon.format=fui::BitmapFormat::BW1;}}
  tab.veThanhTheTenor(cs,{16,620,448,60},items,4,{});assert(tab.renderer.draws>0);
  int before=tab.renderer.draws;tab.uiTarget.setPaintingEnabled(false);tab.veThanhTheTenor(us,{16,620,448,60},items,4,{});
  assert(tab.renderer.draws==before);assert(cross.count()==ugly.count());assert(cross.count()==(touch?4:0));
  for(size_t i=0;i<cross.count();++i){auto c=cross.data()[i],u=ugly.data()[i];assert(c.action==u.action&&c.value==u.value);assert(c.rect.x==u.rect.x&&c.rect.y==u.rect.y&&c.rect.width==u.rect.width&&c.rect.height==u.rect.height);}
 }
 puts("GREEN production list cleanup/navigation preserved, SDK + direct tab draw calls muted, touch hits identical, default-on unchanged");
}
'''
(a.output/'paint.cpp').write_text(cpp)
sdk=a.repo/'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-I'+str(a.repo/'test/ugly_reader/stubs'),'-I'+str(sdk/'include'),str(a.output/'paint.cpp'),str(sdk/'src/FreeInkUI.cpp'),'-o',str(a.output/'paint')],check=True)
subprocess.run([str(a.output/'paint')],check=True)
