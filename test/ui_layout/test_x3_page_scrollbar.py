"""Run the shared production scrollbar on both boards and check X3 wiring."""
import argparse
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mutate-inside', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
cpp = r'''
#include <FreeInkUI.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <vector>
namespace fui=freeink::ui;
uint32_t now=100; int clockReads=0; uint32_t millis(){++clockReads;return now;}
struct Target:fui::DrawTarget {
 std::vector<fui::Rect> fills; std::vector<int> radii;
 fui::Size measureText(fui::FontId,const char*,fui::TextStyle)const override{return{};}
 int16_t lineHeight(fui::FontId)const override{return 24;}
 void fill(fui::Rect r,fui::Paint,uint8_t radius,uint8_t)override{fills.push_back(r);radii.push_back(radius);}
 void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t,uint8_t)override{}
 void line(fui::Point,fui::Point,uint8_t,fui::Paint)override{}
 void triangle(fui::Point,fui::Point,fui::Point,fui::Paint)override{}
 void text(fui::Rect,const char*,fui::TextStyle)override{}
 void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint,fui::Rotation)override{}
};
namespace tenorchrome {constexpr bool kTouchShell=TOUCH;constexpr int FRAME_BAR_WIDTH=6;}
struct GfxRenderer {
 mutable int windows=0;
 int getScreenWidth()const{return TOUCH?480:528;}
 void fillRect(int,int,int,int,bool)const{}
 void waitRefreshComplete()const{}
 void displayWindow(int,int,int,int)const{++windows;}
};
struct MappedInputManager {
 bool wasVerticalSwipe(int&,unsigned long&)const{return false;}
 bool isScreenTouchHeld(int&,int&)const{return false;}
};
Target makeUiTarget(const GfxRenderer&){return{};}
'''
for name in ('PageScrollbarIdle.h', 'PageScrollbar.h'):
    s = (root / 'src/components' / name).read_text()
    if a.mutate_inside and name == 'PageScrollbar.h':
        s = s.replace('renderer.getScreenWidth() - PAGE_SCROLLBAR_EDGE_INSET - tenorchrome::FRAME_BAR_WIDTH',
                      'renderer.getScreenWidth() - 16 - tenorchrome::FRAME_BAR_WIDTH')
    cpp += re.sub(r'^#(?:pragma|include)[^\n]*\n', '', s, flags=re.M)
cpp += r'''
int main(){
 GfxRenderer r; MappedInputManager input; auto& bar=PageScrollbar::instance();
 bar.activate(&r);
 for(int height:{100,310,620}) for(int length:{height+1,height*2,height*10}) {
  for(int offset:{-20,0,(length-height)/2,length-height,length+10}) {
   Target t; bar.beginPaint(); bar.draw(r,t,50,50+height,length,offset,&r);bar.endPaint(&r);
   assert(t.fills.size()==2); auto thumb=t.fills.back();
   assert(thumb.x==r.getScreenWidth()-11 && thumb.width==6);
   assert(thumb.x-(r.getScreenWidth()-17)-1==5);
   assert(t.radii.back()==3);
   assert(thumb.height==std::max(12,height*height/length));
   assert(thumb.y>=50 && thumb.bottom()<=50+height);
   if(offset<=0)assert(thumb.y==50);
   if(offset>=length-height)assert(thumb.bottom()==50+height);
  }
 }
 {Target t;bar.draw(r,t,50,360,620,310,&r,400);assert(t.fills.back().x==(TOUCH?469:405));}
 now+=3000;
 assert(bar.needsIdleUpdate(input)==bool(TOUCH));
 if(TOUCH){assert(bar.renderIdleUpdate(r,input));assert(r.windows==1);}
 else {assert(!bar.renderIdleUpdate(r,input));Target t;bar.repaint(r,t);assert(t.fills.size()==2);assert(r.windows==0);assert(clockReads==0);}
 for(int length:{0,1,310}){Target t;bar.draw(r,t,50,360,length,0,&r);assert(t.fills.empty());}
}
'''
sdk = root / 'freeink-sdk/libs/ui/FreeInkUI'
for touch in (0, 1):
    source = a.output / f'scroll-{touch}.cpp'
    binary = a.output / f'scroll-{touch}'
    source.write_text(cpp.replace('TOUCH', str(touch)))
    subprocess.run(['c++', '-std=c++20', '-I' + str(sdk / 'include'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
parent = (root / 'src/activities/UiListActivity.cpp').read_text()
toolbar = (root / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
popup = (root / 'src/components/OptionPopup.h').read_text()
assert 'props.scrollIndicator = false;' in parent[parent.index('void UiListActivity::frameRows'):parent.index('namespace {', parent.index('void UiListActivity::frameRows'))]
assert 'setPageScrollRegion(screen.body().y' in parent[parent.index('void UiListActivity::measurePageScrollbar'):parent.index('void UiListActivity::drawPageScrollbar')]
assert 'measurePageScrollbar(screen, laid);' in parent[parent.index('void UiListActivity::syncListViewport'):parent.index('void UiListActivity::renderUi')]
tabbed = (root / 'src/activities/UiTabListActivity.cpp').read_text()
assert 'measurePageScrollbar(screen, laid);' in tabbed[tabbed.index('void UiTabListActivity::syncTabListViewport'):]
panel = toolbar[toolbar.index('void ReaderToolbarUi::buildPanel'):toolbar.index('#if defined(FREEINK_DEVICE_X4PRO)', toolbar.index('  const int pageRows = nav_.pageRows();'))]
assert 'listProps_.scrollIndicator = false;' in panel
assert 'PageScrollbar::instance().drawList(' in panel
assert 'PageScrollbar::instance().draw(renderer, target,' in popup
render = toolbar[toolbar.index('void ReaderToolbarUi::render()'):toolbar.index('void ReaderToolbarUi::fadeMoreBelow()')]
assert 'if (usesX3Bar()) {' in render
assert render.index('scrollbar.endPaint();') < render.index('renderUi();')
assert render.index('scrollbar.endPaint();') < render.rindex('scrollbar.beginPaint();')
print('GREEN: 2 boards, 45 geometry cases each, X3 idle, short lists and shared caller wiring')
