#include <FreeInkUIGfxRenderer.h>
#include <cassert>
#include <cstdio>
namespace fui=freeink::ui;
void drawing(fui::GfxRendererTarget& target){
 const fui::Rect box{20,20,100,48};const auto ink=fui::Paint::solid(fui::Color::Black);static const uint8_t pixels[]={255};
 target.fill(box,ink);target.stroke(box,ink,1);target.line({0,0},{10,10},1,ink);
 target.triangle({0,0},{10,0},{10,10},ink);target.text(box,"Test",{});
 fui::BitmapRef bitmap;bitmap.data=pixels;bitmap.width=8;bitmap.height=1;bitmap.format=fui::BitmapFormat::BW1;
 target.bitmap({20,20,8,1},bitmap,fui::BitmapMode::Center);
}
int main(){
 GfxRenderer renderer;fui::GfxRendererTarget target(renderer);assert(target.paintingEnabled());
 drawing(target);assert(renderer.draws>=6);
 int before=renderer.draws;target.setPaintingEnabled(false);drawing(target);assert(renderer.draws==before);
 auto measured=target.measureText(0,"Option",{});assert(measured.width==48&&measured.height==24);
 assert(target.lineHeight(0)==24);target.setClipRect({1,2,300,400});assert(target.clipRect().width==300);
 fui::InteractionBuffer<8> cross,ugly;fui::DeviceContext device=target.deviceContext();fui::InputSnapshot input;
 fui::Frame<8> cf(target,device,input,cross),uf(target,device,input,ugly);
 fui::DialogOption opts[]={{"First",1,0},{"Second",1,1,fui::StateFocused}};
 fui::OptionDialogProps props;props.title="Reader options";props.options=opts;props.optionCount=2;props.verticalOptions=true;
 target.setPaintingEnabled(true);fui::optionDialog(cf,{20,20,300,180},props);before=renderer.draws;
 target.setPaintingEnabled(false);int measurements=renderer.measures;fui::optionDialog(uf,{20,20,300,180},props);
 assert(renderer.draws==before);assert(renderer.measures>measurements);assert(cross.count()==ugly.count());
 for(size_t i=0;i<cross.count();++i){const auto&c=cross.data()[i];const auto&u=ugly.data()[i];assert(c.action==u.action&&c.value==u.value&&c.state==u.state);assert(c.rect.x==u.rect.x&&c.rect.y==u.rect.y&&c.rect.width==u.rect.width&&c.rect.height==u.rect.height);}
 target.setPaintingEnabled(true);drawing(target);assert(renderer.draws>before);
 puts("GREEN real SDK adapter default-on, 6 draw operations muted, measures/clips/hit tables preserved, restore-on");
}
