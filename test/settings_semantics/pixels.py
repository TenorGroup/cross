"""Raster the production popup mark and exercise the shared switch painter."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
text = (a.repo / 'src/components/TenorMenuChrome.cpp').read_text()
start = text.index('void tenorchrome::drawSettingsPopupMark(')
end = text.index('\n}', start) + 2
method = text[start:end]
cpp = r'''
#include <FreeInkUI.h>
#include "components/OptionPopupLayout.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>
namespace fui=freeink::ui;
struct GfxRenderer {
  mutable std::set<std::pair<int,int>> pixels;
  void drawPixel(int x,int y,bool black)const{assert(black);pixels.emplace(x,y);}
};
namespace tenorchrome {void drawSettingsPopupMark(const GfxRenderer&,int,int);}
''' + method + r'''
struct Target:fui::DrawTarget {
  struct Draw {fui::Rect r;fui::Paint p;int radius,width;};
  std::vector<Draw> fills,strokes;
  fui::Size measureText(fui::FontId,const char*,fui::TextStyle)const override{return{};}
  int16_t lineHeight(fui::FontId)const override{return 0;}
  void fill(fui::Rect r,fui::Paint p,uint8_t radius,uint8_t)override{fills.push_back({r,p,radius,0});}
  void stroke(fui::Rect r,fui::Paint p,uint8_t width,uint8_t radius,uint8_t)override{strokes.push_back({r,p,radius,width});}
  void line(fui::Point,fui::Point,uint8_t,fui::Paint)override{}
  void triangle(fui::Point,fui::Point,fui::Point,fui::Paint)override{}
  void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint,fui::Rotation)override{}
  void text(fui::Rect,const char*,fui::TextStyle)override{}
};
int main(){
  const auto x3=optionPopupLayout({0,0,528,792},{},126,200,false);
  assert(x3.width==126&&x3.x==201&&x3.y==296);
  const auto capped=optionPopupLayout({20,48,396,660},{},700,200,false);
  assert(capped.width==396&&capped.x==20&&capped.y==278);
  GfxRenderer r;tenorchrome::drawSettingsPopupMark(r,10,300);
  int ymin=999,ymax=-999;
  for(int x=10;x<=22;x++){
    std::vector<int> ys;
    for(auto [px,py]:r.pixels)if(px==x){ys.push_back(py);ymin=std::min(ymin,py);ymax=std::max(ymax,py);}
    assert(ys.size()==4 && ys[1]==ys[0]+1 && ys[3]==ys[2]+1);
  }
  assert((ymin+ymax)/2.0==299.5);
  for(bool on:{false,true}){
    Target t;fui::DeviceContext d;d.width=480;d.height=800;
    fui::InputSnapshot input;fui::InteractionBuffer<1> hits;fui::Frame<1> frame(t,d,input,hits);
    fui::ToggleProps sw;sw.checked=on;sw.width=46;sw.height=26;sw.radius=13;
    sw.knobRadius=9;sw.knobInset=4;sw.borderWidth=2;
    fui::toggle(frame,{100,200,46,26},sw);
    assert(t.fills.size()==2&&t.strokes.size()==1);
    const auto track=t.fills[0],knob=t.fills[1],border=t.strokes[0];
    assert(track.r.width==46&&track.r.height==26&&track.radius==13);
    assert(border.width==2&&border.p.kind==fui::PaintKind::Solid&&border.p.color==fui::Color::Black);
    assert(knob.r.width==18&&knob.r.height==18&&knob.radius==9&&knob.r.y==204);
    assert(knob.r.x==(on?124:104));
    assert(track.p.kind==fui::PaintKind::Solid&&knob.p.kind==fui::PaintKind::Solid);
    assert(track.p.color==(on?fui::Color::Black:fui::Color::White));
    assert(knob.p.color==(on?fui::Color::White:fui::Color::Black));
  }
  puts("GREEN continuous 2 px popup chevrons, half-pixel center, 46x26 switch OFF/ON, 4 px knob inset, solid ink");
}
'''
source = a.output / 'pixels.cpp'
source.write_text(cpp)
sdk = a.repo / 'freeink-sdk/libs/ui/FreeInkUI'
exe = a.output / 'pixels'
subprocess.run(['c++', '-std=c++17', '-I'+str(sdk/'include'), '-I'+str(a.repo/'src'), str(source),
                str(sdk/'src/FreeInkUI.cpp'), '-o', str(exe)], check=True)
raise SystemExit(subprocess.run([str(exe)]).returncode)
