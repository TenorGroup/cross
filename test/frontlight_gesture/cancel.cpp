#include <FreeInkUICore.h>
#include <FreeInkApp.h>
#include <cassert>
using namespace freeink::ui;
struct Target : DrawTarget {
  Size measureText(FontId,const char*,TextStyle) const override {return {};}
  int16_t lineHeight(FontId) const override {return 20;}
  void fill(Rect,Paint,uint8_t,uint8_t) override {}
  void stroke(Rect,Paint,uint8_t,uint8_t,uint8_t) override {}
  void line(Point,Point,uint8_t,Paint) override {}
  void triangle(Point,Point,Point,Paint) override {}
  void text(Rect,const char*,TextStyle) override {}
  void bitmap(Rect,BitmapRef,BitmapMode,Paint,Rotation) override {}
};
using App=FreeInkApp<8,2>;
void screen(App::ScreenType& s,void*) {
  s.frame().hit({20,20,100,30},1,0,InputDrag|InputTouch|InputFocus);
  s.frame().hit({200,20,100,30},2,0,InputTouch|InputFocus);
}
void counted(const ActionEvent&,void* user) {++*static_cast<int*>(user);}
// Legacy baseline compiles too, so RED records the stale-binding behavior.
template<class T> auto cancel(T& buffer,int)->decltype(buffer.cancelTouchContact(),void()) {buffer.cancelTouchContact();}
template<class T> void cancel(T&,long) {}
int main(){
  InteractionBuffer<8> buffer;
  buffer.addInteraction({{20,20,100,30},1,0,InputDrag|InputTouch|InputFocus});
  buffer.addInteraction({{200,20,100,30},2,0,InputTouch|InputFocus});
  buffer.setFocusedIndex(1);
  InputSnapshot held{};held.touchHeld=true;held.touchX=50;held.touchY=30;
  const auto drag=buffer.route(held);
  assert(drag.action==1 && drag.dragPermille>=0);
  // A classified global multi-gesture consumes the physical release.
  cancel(buffer,0);
  assert(buffer.focusedIndex()==1 && buffer.count()==2);
  // Fresh contact outside the slider: the old binding must emit no drag.
  held.touchX=220;
  const auto fresh=buffer.route(held);
  assert(!fresh);
  InputSnapshot tap{};tap.touchPressed=true;tap.touchX=220;tap.touchY=30;
  assert(!buffer.route(tap));
  tap.touchPressed=false;tap.touchReleased=true;
  const auto tapped=buffer.route(tap);
  assert(tapped.action==2 && tapped.dragPermille==-1);
  // An ordinary 1-finger drag keeps one release callback after cancellation.
  held.touchX=70;
  assert(buffer.route(held).action==1);
  InputSnapshot released{};released.touchReleased=true;released.touchX=-1;released.touchY=-1;
  const auto committed=buffer.route(released);
  assert(committed.action==1 && committed.dragPermille>=0);
  assert(!buffer.route(released));
  Target target;DeviceContext device;device.width=400;device.height=300;
  App app(target,device);int sliderCalls=0,tapCalls=0;
  app.setScreen(screen);app.on(1,counted,&sliderCalls);app.on(2,counted,&tapCalls);app.render();
  held.touchX=50;assert(app.route(held).action==1 && sliderCalls==1);
  app.cancelTouchContact();
  assert(!app.touchActive() && sliderCalls==1 && tapCalls==0);
  held.touchX=220;assert(!app.route(held) && sliderCalls==1);
  tap.touchPressed=true;tap.touchReleased=false;assert(!app.route(tap));
  tap.touchPressed=false;tap.touchReleased=true;
  assert(app.route(tap).action==2 && tapCalls==1 && sliderCalls==1);
  held.touchX=70;app.route(held);const int beforeRelease=sliderCalls;
  assert(app.route(released).action==1 && sliderCalls==beforeRelease+1);
  assert(!app.route(released) && sliderCalls==beforeRelease+1);
}
