"""Run production fixed-frame paint and input routing on the real UI hit table."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mutation', action='store_true')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)

def method(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

popup = (a.repo / 'src/components/OptionPopup.h').read_text()
if a.mutation:
    popup = popup.replace('const int menuTop = actions ? top - 8 : top;', 'const int menuTop = top;')
hold_source=(a.repo / 'src/MappedInputManager.cpp').read_text()
if 'bool MappedInputManager::wasScreenHold(' in hold_source:
    hold=method(hold_source, 'bool MappedInputManager::wasScreenHold(')
else:
    hold=method(hold_source, 'bool MappedInputManager::wasScreenLongPress(').replace(
        'wasScreenLongPress(int& x, int& y)', 'wasScreenHold(int& x, int& y, unsigned long thresholdMs)')
hold=hold.replace('MappedInputManager::','')
caller = "Home context actions"
lines = ""
home=(a.repo / "src/activities/home/HomeActivity.cpp").read_text()
if "void HomeActivity::onRowAction(" in home:
    action=method(home,"void HomeActivity::onRowAction(")
    assert action.index("onRowLongPress") < action.index("UiTabListActivity::onRowAction"), "hold changed the shown book"

tick_asset = (a.repo / "src/components/icons/tenorRowMarks.h").read_text()
tick_bytes = tick_asset.split("icon_row_chosen_24_bits[] = {", 1)[1].split("};", 1)[0]
cpp = r'''
#include <FreeInkUI.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "components/OptionPopupLayout.h"
#include "components/SettingsChoiceStyle.h"
#include "components/TouchScroll.h"
namespace fui=freeink::ui;
constexpr int UI_12_FONT_ID=1;
enum class Color {White};
struct EpdFontFamily {enum Style {BOLD,REGULAR};};
const unsigned char tickBits[] = {''' + tick_bytes + r'''};
struct Icon {const unsigned char* bits=tickBits;} icon_row_chosen_24;
struct GfxRenderer {
 void tapToLogical(float nx,float ny,int&x,int&y)const{x=nx*480;y=ny*800;}
 mutable std::vector<fui::Rect> ink;
 mutable std::vector<std::string> labels;
 int inkHeight=32,lineHeight=33;
 int getLineHeight(int)const{return lineHeight;}
 int getTextInkTop(int,const char*,EpdFontFamily::Style)const{return 0;}
 int getTextInkBottom(int,const char*,EpdFontFamily::Style)const{return inkHeight;}
 int getTextWidth(int,const char*s,EpdFontFamily::Style=EpdFontFamily::REGULAR)const{return strlen(s)*12;}
 std::string truncatedText(int,const char*s,int room,EpdFontFamily::Style)const{return std::string(s).substr(0,std::max(0,room)/12);}
 void fillRoundedRect(int,int,int,int,int,Color)const{}
 void fillRect(int,int,int,int,bool)const{}
 void drawLine(int,int,int,int)const{}
 void drawText(int,int x,int y,const char*s,bool,EpdFontFamily::Style)const{labels.emplace_back(s);ink.push_back({(int16_t)x,(int16_t)y,(int16_t)(strlen(s)*12),(int16_t)inkHeight});}
};
struct Target:fui::DrawTarget {
 std::vector<fui::Rect> ink;
 fui::Size measureText(fui::FontId,const char*s,fui::TextStyle)const override{return{(int16_t)(strlen(s)*12),33};}
 int16_t lineHeight(fui::FontId)const override{return 33;}
 void fill(fui::Rect,fui::Paint,uint8_t,uint8_t)override{}
 void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t,uint8_t)override{}
 void line(fui::Point,fui::Point,uint8_t,fui::Paint)override{}
 void triangle(fui::Point,fui::Point,fui::Point,fui::Paint)override{}
 void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint,fui::Rotation)override{}
 void text(fui::Rect r,const char*s,fui::TextStyle style)override{fui::layoutText(*this,r,s,style,[&](const char*,fui::Rect run){ink.push_back(run);});}
};
namespace freeink::ui {struct GfxRendererTarget {static constexpr int FONT_BODY=1;};}
struct PageScrollbar {
 static PageScrollbar& instance(){static PageScrollbar scrollbar;return scrollbar;}
 void draw(const GfxRenderer&,fui::DrawTarget&,int,int,int,int,const void*){}
};
namespace shell {bool isUgly(){return false;}}
namespace tenorchrome {
void drawContextActionIcon(const GfxRenderer&r,int,int x,int y){r.ink.push_back({(int16_t)x,(int16_t)y,24,24});}
constexpr bool kTouchShell=true;constexpr int FOOT_BACK_X=16,PANEL_RADIUS=20;
int contentTop(){return 38;}int footBackTop(int h){return h-76;}
void drawRoundRing(const GfxRenderer&,int,int,int,int,int,int,bool){}
void drawRowRule(const GfxRenderer&,int,int,int){}
void drawBarIcon(const GfxRenderer&r,const unsigned char*,int w,int h,int x,int y,bool){r.ink.push_back({(int16_t)x,(int16_t)y,(int16_t)w,(int16_t)h});}
struct Bar {int x,y,width,height;};
Bar frameScrollBar(int x,int,int w,int,int top,int bottom){return{x+w-12,top,4,bottom-top};}
}
struct MappedInputManager {
 enum class Button {NavPrevious,NavNext,Confirm,Back};
 fui::InputSnapshot snap;bool swipe=false;int dy=0;unsigned long ms=0;bool back=false;
 bool wasVerticalSwipe(int&d,unsigned long&t){d=dy;t=ms;return swipe;}
 bool wasPressed(Button){return false;}
 bool wasReleased(Button b){return b==Button::Back&&back;}
};
fui::InputSnapshot touchSnapshotFrom(const MappedInputManager&i){return i.snap;}
struct Lines {int rule,top,bottom;};
''' + lines + r'''
struct Popup {
 bool contextMenu=true,contextPinned=false;
 std::function<void(fui::Rect)> contextBackground;
 static constexpr int MAX_OPTIONS=16,ACTION_OPTION=1,ACTION_CHROME=2,ACTION_PAGE=3;
 bool anchored=false,inFrame=true,hasAnchor=false,alignSelected=false,marked=false,active=true,uiReady=true;
 fui::Rect frameRect,anchor;mutable fui::Rect lastRenderedFrame;
 mutable bool headLaid=false;mutable int boldLines=0,scrollTop=-1,shownRows=16,shownPitch=56;
 mutable std::vector<std::string> headLines;std::string title="Paired devices",headline;
 std::vector<std::string> ownedStrings{"Connect","Forget"};int selectedIndex=0;
 std::function<void(int)> onSelectCallback;mutable fui::InteractionBuffer<17> interactions;
 static int rowsInFrame(int h,int head,int row,int count){return optionPopupFrameRows(h,head,row,count);}
 static void uglyPaper(const GfxRenderer&,fui::Rect){}
 static void uglyText(const GfxRenderer&,fui::Rect,const char*){}
''' + (method(popup, '  static void drawAction(') if '  static void drawAction(' in popup else '') + '\n' + method(popup, '  static void drawChoice(') + '\n' + 'template<typename Frame>\n' + method(popup, '  void renderAnchored(') + '\n' + method(popup, '  bool handleInput(') + r'''
 void paint(GfxRenderer&r,Target&t,fui::DeviceContext&d){
   interactions.beginPublishCycle();fui::InputSnapshot input;
   fui::Frame<17> frame(t,d,input,interactions);renderAnchored(r,frame,d.screen());interactions.publish();
 }
};
bool inside(fui::Rect r,fui::Rect box){return r.x>=box.x&&r.y>=box.y&&r.right()<=box.right()&&r.bottom()<=box.bottom();}
struct HoldGpio {
 unsigned long held=0;bool down=true,moved=false,suppressed=false;
 bool isTouchTapCandidate(float&x,float&y,unsigned long&ms)const{
  if(!down||moved||suppressed)return false;x=.5f;y=.5f;ms=held;return true;
 }
 bool wasTouchLongPress(float&x,float&y)const{unsigned long ms=0;return isTouchTapCandidate(x,y,ms)&&ms>=500;}
 void suppressTouchContact(){suppressed=true;}
};
struct HoldInput {HoldGpio&gpio;const GfxRenderer&renderer;
''' + hold + r'''
};
int main(){
 HoldGpio gpio;GfxRenderer holdRenderer;HoldInput input{gpio,holdRenderer};int tx=0,ty=0;
 gpio.held=699;assert(!input.wasScreenHold(tx,ty,700));assert(!gpio.suppressed);
 gpio.held=700;assert(input.wasScreenHold(tx,ty,700)&&tx==240&&ty==400);assert(gpio.suppressed);
 assert(!input.wasScreenHold(tx,ty,700));gpio.suppressed=false;gpio.down=false;
 assert(!input.wasScreenHold(tx,ty,700));gpio.down=true;gpio.moved=true;gpio.held=900;
 assert(!input.wasScreenHold(tx,ty,700));
 printf("GREEN hold: 699 blocked, 700 fires once, release and moved contact rejected\n");
 int scenarios=0;
 for(int sourceHeight:{100,425})for(int anchorY:{80,184,300,550})for(int tier:{0,1,2})for(int ax:{17,93,169,321})for(int labelWidth:{36,224,288,600}){
   Popup p;p.inFrame=false;p.anchored=p.hasAnchor=true;p.marked=false;
   p.anchor={(int16_t)ax,(int16_t)anchorY,120,(int16_t)sourceHeight};
   p.ownedStrings={"Pin",std::string(labelWidth/12,'R')};
   GfxRenderer r;r.lineHeight=33+tier*5;Target t;fui::DeviceContext d;d.width=480;d.height=800;
   p.paint(r,t,d);auto b=p.lastRenderedFrame;
   const int expectedWidth=std::clamp(std::max(36,labelWidth/12*12)+68,200,360);
   const int expectedX=std::min(ax,464-expectedWidth);
   const int expectedY=p.anchor.bottom()+8+b.height<=716?p.anchor.bottom()+8:p.anchor.y-8-b.height;
   if(p.shownPitch!=60||b.width<200||b.width>360||b.y!=expectedY||b.bottom()>716||b.width!=expectedWidth||b.x!=expectedX||b.height!=140){
    printf("RED action geometry anchor=%d,%d row=%d box=%d,%d,%d,%d expectedY=%d\n",ax,anchorY,p.shownPitch,b.x,b.y,b.width,b.height,expectedY);return 1;
   }
   assert(b.bottom()<=p.anchor.y-8 || b.y>=p.anchor.bottom()+8);
   assert(b.x>=16&&b.right()<=464);
   if(labelWidth<=292)assert(r.labels==p.ownedStrings);
   int chosen=-1,refresh=0;p.onSelectCallback=[&](int n){chosen=n;};
   MappedInputManager input;input.snap.touchX=479;input.snap.touchY=40;input.snap.touchReleased=true;
   p.handleInput(input,[&]{++refresh;});assert(!p.active&&chosen==-1&&refresh==1);
   p.active=true;refresh=0;input.snap={};input.back=true;p.handleInput(input,[&]{++refresh;});assert(!p.active&&chosen==-1&&refresh==1);
   if(b.right()+4<480) {
     p.active=true;refresh=0;chosen=-1;input.back=false;input.snap={};
     input.snap.touchX=b.right()+4;input.snap.touchY=b.y+36;input.snap.touchReleased=true;
     p.handleInput(input,[&]{++refresh;});
     if(p.active || chosen!=-1 || refresh!=1) {
       printf("RED outside clipped menu x=%d right=%d chosen=%d active=%d\n",input.snap.touchX,b.right(),chosen,p.active);
       return 1;
     }
   }
   for(int bx:{20,100,190,280,365,455})for(int by:{724,754,783}) {
     p.active=true;refresh=0;chosen=-1;input.back=false;input.snap={};
     input.snap.touchX=bx;input.snap.touchY=by;input.snap.touchReleased=true;
     p.handleInput(input,[&]{++refresh;});
     assert(!p.active&&chosen==-1&&refresh==1);
   }
   ++scenarios;
 }
 printf("GREEN %d action renders, real SDK hit table, outside and Back cancel\n",scenarios);
}
'''
path = a.output / 'production-popup.cpp'
path.write_text(cpp)
exe = a.output / 'production-popup'
sdk = a.repo / 'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-I' + str(sdk / 'include'), '-I' + str(a.repo / 'src'), str(path),
                str(sdk / 'src/FreeInkUI.cpp'), '-o', str(exe)], check=True)
run = subprocess.run([str(exe)], capture_output=True, text=True)
(a.output / 'run.log').write_text(run.stdout + run.stderr)
(a.output / 'result.json').write_text(json.dumps({'returncode': run.returncode,
    'caller': caller, 'row_frame_lines': lines,
    'popup_sha256': hashlib.sha256((a.repo / 'src/components/OptionPopup.h').read_bytes()).hexdigest(),
    'boundary': 'Production popup render/input and row-frame padding, SDK hit table; fake draw and BLE state.'}, indent=2) + '\n')
print(run.stdout + run.stderr, end='')
if a.mutation:
    assert run.returncode != 0, 'top clamp mutation survived'
    print('GREEN: top clamp mutation rejected')
    raise SystemExit(0)
raise SystemExit(run.returncode)
