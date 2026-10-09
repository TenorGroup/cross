"""Run production fixed-frame paint and input routing on the real UI hit table."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument('--output', type=Path, required=True)
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
ble = (a.repo / 'src/activities/settings/BlePageTurnerActivity.cpp').read_text()
parent = (a.repo / 'src/activities/UiListActivity.cpp').read_text()
caller = method(ble, 'void BlePageTurnerActivity::openPairedPopup(')
assert 'rowFrameFor(ACTION_ROW, nav.selected, parentFrame)' in caller
assert 'optionPopup.showInFrame(parentFrame' in caller
assert 'sectionHeading = tr(STR_BLE_PAIRED_DEVICES)' in ble
lines = method(parent, 'UiListActivity::RowFrameLines UiListActivity::rowFrameLines(')
lines = lines.replace('UiListActivity::RowFrameLines UiListActivity::rowFrameLines', 'Lines rowFrameLines')
cpp = r'''
#include <FreeInkUI.h>
#include <algorithm>
#include <cassert>
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
struct Icon {const unsigned char* bits=nullptr;} icon_row_chosen_24;
struct GfxRenderer {
 mutable std::vector<fui::Rect> ink;
 int getLineHeight(int)const{return 33;}
 int getTextWidth(int,const char*s)const{return strlen(s)*12;}
 std::string truncatedText(int,const char*s,int room,EpdFontFamily::Style)const{return std::string(s).substr(0,std::max(0,room)/12);}
 void fillRoundedRect(int,int,int,int,int,Color)const{}
 void drawText(int,int x,int y,const char*s,bool,EpdFontFamily::Style)const{ink.push_back({(int16_t)x,(int16_t)y,(int16_t)(strlen(s)*12),33});}
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
namespace shell {bool isUgly(){return false;}}
namespace tenorchrome {
constexpr bool kTouchShell=true;constexpr int FOOT_BACK_X=16,PANEL_RADIUS=20;
int contentTop(){return 48;}int footBackTop(int h){return h-76;}
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
''' + 'template<typename Frame>\n' + method(popup, '  void renderAnchored(') + '\n' + method(popup, '  bool handleInput(') + r'''
 void paint(GfxRenderer&r,Target&t,fui::DeviceContext&d){
   interactions.beginPublishCycle();fui::InputSnapshot input;
   fui::Frame<17> frame(t,d,input,interactions);renderAnchored(r,frame,d.screen());interactions.publish();
 }
};
bool inside(fui::Rect r,fui::Rect box){return r.x>=box.x&&r.y>=box.y&&r.right()<=box.right()&&r.bottom()<=box.bottom();}
int main(){
 for (int count = 0; count <= 20; ++count) {
   assert(settingsChoiceStyle(count, true) == (count <= 2 ? SettingsChoiceStyle::Inline :
       count < SETTINGS_CHOICE_PAGE_THRESHOLD ? SettingsChoiceStyle::Popup : SettingsChoiceStyle::Page));
   assert(settingsChoiceStyle(count, false) == (count <= 2 ? SettingsChoiceStyle::Inline : SettingsChoiceStyle::Popup));
 }
 const auto parentLines=rowFrameLines(6);
 const int bondFrameHeight=56+parentLines.top+parentLines.bottom;
 assert(bondFrameHeight==65);
 int scenarios=0;
 for(int h:{bondFrameHeight,80,198,350})for(int longTitle:{0,1})for(int selected:{0,1}){
   Popup popup;popup.frameRect={16,360,448,(int16_t)h};popup.selectedIndex=selected;
   if(longTitle)popup.title="A long paired device heading wraps across several lines";
   GfxRenderer renderer;Target target;fui::DeviceContext device;device.width=480;device.height=800;
   popup.paint(renderer,target,device);
   for(size_t i=0;i<popup.interactions.publishedCount();++i){
     const auto hit=popup.interactions.publishedData()[i];
     if(!inside(hit.rect,popup.frameRect)){fprintf(stderr,"RED h=%d hit bottom=%d frame bottom=%d\n",h,hit.rect.bottom(),popup.frameRect.bottom());return 1;}
   }
   for(auto rect:renderer.ink)assert(inside(rect,popup.frameRect));
   for(auto rect:target.ink)assert(inside(rect,popup.frameRect));
   int chosen=-1;popup.onSelectCallback=[&](int n){chosen=n;};
   // Every option remains reachable in the narrow bond frame by swiping.
   if(popup.shownRows==1&&popup.scrollTop==0){
     MappedInputManager swipe;swipe.swipe=true;swipe.dy=-60;swipe.ms=100;
     popup.handleInput(swipe,[]{});popup.paint(renderer,target,device);assert(popup.scrollTop==1);
     swipe.dy=60;popup.handleInput(swipe,[]{});popup.paint(renderer,target,device);assert(popup.scrollTop==0);
   }
   fui::Interaction option{};
   for(size_t i=0;i<popup.interactions.publishedCount();++i)if(popup.interactions.publishedData()[i].action==1&&popup.interactions.publishedData()[i].value==selected)option=popup.interactions.publishedData()[i];
   assert(option.action==1);
   MappedInputManager input;input.snap.touchX=option.rect.x+option.rect.width/2;input.snap.touchY=option.rect.y+option.rect.height/2;
   input.snap.touchPressed=true;popup.handleInput(input,[]{});assert(chosen==-1&&popup.active);
   input.snap.touchPressed=false;input.snap.touchReleased=true;popup.handleInput(input,[]{});assert(chosen==option.value&&!popup.active);
   popup.active=true;input.snap={};input.back=true;chosen=-1;popup.handleInput(input,[]{});assert(!popup.active&&chosen==-1);
   ++scenarios;
 }
 for(int count:{3,4,5,6})for(int selected=0;selected<count;++selected)for(int tapY:{80,360,660}) {
   Popup popup;popup.inFrame=false;popup.hasAnchor=popup.alignSelected=popup.marked=true;
   popup.anchor={16,(int16_t)(tapY-28),448,56};popup.selectedIndex=selected;
   popup.ownedStrings.assign(count,"Value");
   GfxRenderer renderer;Target target;fui::DeviceContext device;device.width=480;device.height=800;
   popup.paint(renderer,target,device);
   assert(popup.shownRows==count&&popup.scrollTop==0);
   assert(popup.lastRenderedFrame.height==count*56+12);
   assert(popup.lastRenderedFrame.y>=48&&popup.lastRenderedFrame.bottom()<=716);
   int selectedY=popup.lastRenderedFrame.y+6+selected*56+28;
   const int expectedY=std::max(48+6+selected*56+28,std::min(716-(count-selected-1)*56-6-28,tapY));
   assert(selectedY==expectedY);
   for(size_t index=0;index<popup.interactions.publishedCount();++index)
     assert(inside(popup.interactions.publishedData()[index].rect,popup.lastRenderedFrame));
   int chosen=-1;popup.onSelectCallback=[&](int value){chosen=value;};
   MappedInputManager input;input.snap.touchX=479;input.snap.touchY=48;input.snap.touchReleased=true;
   popup.handleInput(input,[]{});assert(!popup.active&&chosen==-1);
   ++scenarios;
 }
 printf("GREEN %d renders: fixed-frame U11, settings thresholds, selected-row anchoring, clamping, outside close\n",scenarios);
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
raise SystemExit(run.returncode)
