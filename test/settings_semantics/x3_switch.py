"""Exercise production row flags and switch geometry through the shared UI painter."""
import argparse,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];a.output.mkdir(parents=True,exist_ok=True)
files=['settings/SettingsActivity.cpp','settings/TextSettingsActivity.cpp','settings/ClockSettingsActivity.cpp','settings/KOReaderSettingsActivity.cpp','reader/EpubReaderMenuActivity.cpp']
checks=[]
for filename in files:
 text=(root/'src/activities'/filename).read_text()
 assignment=re.search(r'(?:rowItems_|menuRowItems)\[i?row?\]?\.toggle =.*?;',text)
 if not assignment: assignment=re.search(r'\w+\[\w+\]\.toggle =.*?;',text,re.S)
 assert assignment,filename
 expr=assignment[0].split('=',1)[1].strip().rstrip(';')
 expr=re.sub(r'rowChoiceStyle\([^)]*\)','SettingsChoiceStyle::Inline',expr)
 checks.append('ok &= '+expr+';')
chrome=(root/'src/activities/UiListActivity.cpp').read_text()
start=chrome.index('void UiListActivity::frameRows(');end=chrome.index('\nnamespace {',start)
method=chrome[start:end]
config='const int switchScale = tenorchrome::kTouchShell ? 0 : normalizedUiTextSize(SETTINGS.uiTextSize);\n'+'\n'.join(re.findall(r'  props\.toggle\w+ =[^;]+;',method))
# A configuration after the physical early return cannot reach X3.
reachable=method.index('props.toggleWidth') < method.index('if (!rowsFramed)')
cpp=r'''
#include <FreeInkUI.h>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <initializer_list>
#include "components/SettingsChoiceStyle.h"
namespace fui=freeink::ui;
int allocations=0;
void* operator new(size_t n){++allocations;return std::malloc(n);}
void operator delete(void* p)noexcept{std::free(p);}
namespace tenorchrome{bool kTouchShell;}
struct {int uiTextSize;} SETTINGS;
int normalizedUiTextSize(int n){return n;}
struct Target:fui::DrawTarget{
 int fills=0,strokes=0;fui::Rect track,knob;int radius=0;
 fui::Size measureText(fui::FontId,const char*,fui::TextStyle)const override{return{};}
 int16_t lineHeight(fui::FontId)const override{return 0;}
 void fill(fui::Rect r,fui::Paint,uint8_t rad,uint8_t)override{if(fills++==0){track=r;radius=rad;}else knob=r;}
 void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t,uint8_t)override{++strokes;}
 void line(fui::Point,fui::Point,uint8_t,fui::Paint)override{}
 void triangle(fui::Point,fui::Point,fui::Point,fui::Paint)override{}
 void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint,fui::Rotation)override{}
 void text(fui::Rect,const char*,fui::TextStyle)override{}
};
int main(){int bad=0;for(bool touch:{false,true})for(int tier:{0,1,2}){
 tenorchrome::kTouchShell=touch;SETTINGS.uiTextSize=tier;int i=4,row=0;constexpr int ITEM_AUTO_TIMEZONE=4;
 bool ok=true;
'''+'\n'.join(checks)+r'''
 fui::ListProps props;
'''+config+r'''
 ok &= touch || REACHABLE;
 for(bool on:{false,true}){Target target;fui::DeviceContext d;d.width=528;d.height=800;
 fui::InputSnapshot input;fui::InteractionBuffer<1> hits;fui::Frame<1> frame(target,d,input,hits);
 fui::ToggleProps sw;sw.checked=on;sw.width=props.toggleWidth;sw.height=props.toggleHeight;
 sw.radius=props.toggleRadius;sw.knobRadius=props.toggleKnobRadius;
 sw.knobInset=props.toggleKnobInset;sw.borderWidth=props.toggleBorderWidth;
 int before=allocations;fui::toggle(frame,{100,200,sw.width,sw.height},sw);
 ok &= allocations==before && target.fills==2 && target.strokes==1 && target.radius==sw.height/2;
 ok &= target.track.height <= (tier==0?52:tier==1?58:64) && target.knob.width==target.knob.height;
 printf("%s touch=%d size=%d on=%d track=%dx%d knob=%dx%d heap=%d\n",ok?"GREEN":"RED",touch,tier,on,target.track.width,target.track.height,target.knob.width,target.knob.height,allocations-before);
 }bad+=!ok;
}return bad?1:0;}
'''
cpp=cpp.replace('REACHABLE',str(reachable).lower())
source=a.output/'switch.cpp';source.write_text(cpp);exe=a.output/'switch';sdk=root/'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++','-std=c++17','-I'+str(sdk/'include'),'-I'+str(root/'src'),str(source),str(sdk/'src/FreeInkUI.cpp'),'-o',str(exe)],check=True)
raise SystemExit(subprocess.run([str(exe)]).returncode)
