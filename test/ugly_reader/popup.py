"""Compare production Ugly popup hit tables with the real SDK dialog and route release events."""
from pathlib import Path
import argparse
import subprocess
p=argparse.ArgumentParser(); p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]); p.add_argument('--output',type=Path,required=True); a=p.parse_args(); a.output.mkdir(parents=True,exist_ok=True)
s=(a.repo/'src/components/OptionPopup.h').read_text()
assert 'setUglyStyle' not in s, 'RED: a popup asks the shell, no caller opts it in'
assert 'if (shell::isUgly()) renderUgly' in s and 'else fui::optionDialog' in s, 'RED: popup styles must select exact existing SDK branch'
start=s.index('  static void uglyText'); end=s.index('  template <typename Frame>\n  void renderAnchored',start)
helpers=s[start:end]
cpp=r'''
#include <FreeInkUI.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
namespace fui=freeink::ui;
class GfxRenderer {public:
 mutable std::array<int,4> clip{0,0,800,800}; mutable int pen=0;
 auto getClipRect()const{return clip;}
 void setClipRect(int x,int y,int w,int h)const{assert(w>=0&&h>=0);clip={x,y,w,h};}
 void fillRect(int,int,int,int,bool)const{++pen;}
};
namespace ugly {
 enum class Size {S22}; enum class Circle {Row}; struct Box{int x0,y0,x1,y1;};
 int width(const GfxRenderer&,Size,const char* s){return strlen(s)*10;}
 int ascent(Size){return 22;}
 std::string fit(const GfxRenderer&,Size,const std::string&s,int w){return s.substr(0,std::max(0,w)/10);}
 void text(const GfxRenderer&r,Size,int,int,const char*){++r.pen;}
 void line(const GfxRenderer&r,int,int,int,int,int,int){++r.pen;}
 void circle(const GfxRenderer&r,Circle,Box,int,int,int){++r.pen;}
}
struct Target:fui::DrawTarget {
 int tier=0;
 fui::Size measureText(fui::FontId,const char*s,fui::TextStyle)const override{return{static_cast<int16_t>(strlen(s)*(8+tier*2)),static_cast<int16_t>(24+tier*8)};}
 int16_t lineHeight(fui::FontId)const override{return static_cast<int16_t>(24+tier*8);}
 void fill(fui::Rect,fui::Paint,uint8_t,uint8_t)override{}
 void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t,uint8_t)override{}
 void line(fui::Point,fui::Point,uint8_t,fui::Paint)override{}
 void triangle(fui::Point,fui::Point,fui::Point,fui::Paint)override{}
 void text(fui::Rect,const char*,fui::TextStyle)override{}
 void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint,fui::Rotation)override{}
};
struct Popup {
'''+helpers+r'''
};
int main(){
 int scenarios=0;
 for(int tier:{0,1,2})for(auto size:{fui::Size{480,800},fui::Size{800,480},fui::Size{528,792},fui::Size{792,528}})
 for(int touch:{44,60})for(int count:{0,3,5,16}) {
  Target target;target.tier=tier; GfxRenderer renderer;Popup popup;
  fui::DeviceContext device;device.width=size.width;device.height=size.height;device.minTouchSize=touch;
  fui::InputSnapshot input; fui::InteractionBuffer<17> cross,ugly;
  cross.beginPublishCycle();ugly.beginPublishCycle();
  fui::Frame<17> cf(target,device,input,cross),uf(target,device,input,ugly);
  fui::DialogOption options[16];
  for(int i=0;i<count;++i)options[i]={"Reader option",1,static_cast<int16_t>(i),i==count-1?fui::StateFocused:fui::StateNormal,i!=1};
  fui::OptionDialogProps props;props.title="A reader popup title that wraps across lines";props.titleText.maxLines=2;
  props.headline="Book title across several lines";props.headlineText.maxLines=3;
  props.options=options;props.optionCount=count;props.verticalOptions=true;props.inputMask=fui::InputTouch;
  props.buttonHeight=target.lineHeight(0)+24;props.gap=8;
  int16_t width=size.width*3/4,height=fui::optionDialogHeight(target,props,width);
  fui::Rect rect{static_cast<int16_t>((size.width-width)/2),8,width,height};
  cf.hit(rect,2,0,fui::InputTouch);uf.hit(rect,2,0,fui::InputTouch);
  fui::optionDialog(cf,rect,props);popup.renderUgly(renderer,uf,rect,props);
  cross.publish();ugly.publish();assert(cross.publishedCount()==ugly.publishedCount());
  for(size_t i=0;i<cross.publishedCount();++i){
   auto c=cross.publishedData()[i],u=ugly.publishedData()[i];
   assert(c.rect.x==u.rect.x&&c.rect.y==u.rect.y&&c.rect.width==u.rect.width&&c.rect.height==u.rect.height);
   assert(c.action==u.action&&c.value==u.value&&c.inputMask==u.inputMask&&c.state==u.state);
   fui::InputSnapshot press;press.touchPressed=true;press.touchX=c.rect.x+c.rect.width/2;press.touchY=c.rect.y+c.rect.height/2;
   auto ce=cross.routePublished(press),ue=ugly.routePublished(press);assert(ce.action==ue.action&&ce.value==ue.value);
   press.touchPressed=false;press.touchReleased=true;ce=cross.routePublished(press);ue=ugly.routePublished(press);
   assert(ce.action==ue.action&&ce.value==ue.value);
  }
  assert(renderer.pen>0);assert((renderer.clip==std::array<int,4>{0,0,800,800}));++scenarios;
 }
 printf("GREEN %d Ugly popup/SDK hit-table and press-release routing pairs, 3 tiers, 4 orientations, 2 minimum-touch sizes\n",scenarios);
}
'''
(a.output/'popup.cpp').write_text(cpp)
sdk=a.repo/'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-I'+str(sdk/'include'),str(a.output/'popup.cpp'),str(sdk/'src/FreeInkUI.cpp'),'-o',str(a.output/'popup')],check=True)
subprocess.run([str(a.output/'popup')],check=True)
