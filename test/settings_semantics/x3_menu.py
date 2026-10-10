"""Exercise the production text sink through the SDK option-dialog button path."""
import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];a.output.mkdir(parents=True,exist_ok=True)
s=(root/'src/components/OptionPopup.h').read_text()
start=s.index('  static void drawChoice(');end=s.index('\n  }',start)+4;choice=s[start:end]
start=s.index('  static void centerOptionText(');end=s.index('    const auto family =',start);sink=s[start:end]+'  }'
cpp=r'''
#include <FreeInkUI.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
namespace fui=freeink::ui;
struct EpdFontFamily{enum Style{REGULAR,BOLD};};
struct GfxRenderer{
 mutable int tx=-1,mx=-1,my=-1,ty=-1;
 int getTextInkTop(int,const char*,EpdFontFamily::Style)const{return 4;}
 int getTextInkBottom(int,const char*,EpdFontFamily::Style)const{return 27;}
 int getTextWidth(int,const char*)const{return 36;}
 int getLineHeight(int)const{return 33;}
 std::string truncatedText(int,const char*s,int,EpdFontFamily::Style)const{return s;}
 void drawText(int,int x,int y,const char*,bool,EpdFontFamily::Style)const{tx=x;ty=y;}
};
struct{int bodyFontId=12;} scale;
auto uiScaleSpec(){return scale;}
struct Icon{const unsigned char*bits=nullptr;}icon_row_chosen_24;
namespace tenorchrome{void drawBarIcon(const GfxRenderer&r,const unsigned char*,int,int,int x,int y,bool){r.mx=x;r.my=y;}}
struct Target:fui::DrawTarget{
 void(*sink)(void*,fui::Rect,const char*,const fui::TextStyle&)=nullptr;void*ctx=nullptr;
 void setTextSink(decltype(sink) fn,void*c){sink=fn;ctx=c;}
 fui::Size measureText(fui::FontId,const char*,fui::TextStyle)const override{return{36,33};}
 int16_t lineHeight(fui::FontId)const override{return 33;}
 void fill(fui::Rect,fui::Paint,uint8_t,uint8_t)override{}
 void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t,uint8_t)override{}
 void line(fui::Point,fui::Point,uint8_t,fui::Paint)override{}
 void triangle(fui::Point,fui::Point,fui::Point,fui::Paint)override{}
 void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint,fui::Rotation)override{}
 void text(fui::Rect r,const char*s,fui::TextStyle st)override{if(sink)sink(ctx,r,s,st);}
};
namespace freeink::ui{using GfxRendererTarget=::Target;}
struct Popup{
'''+choice+r'''
 struct CenteredText{const GfxRenderer&renderer;Target&target;bool marked;const char*selected;const std::vector<std::string>*values;};
'''+sink+r'''
};
int main(){GfxRenderer r;Target t;std::vector<std::string>labels{"Nho"};
 Popup::CenteredText ctx{r,t,true,"Nho",&labels};t.setTextSink(Popup::centerOptionText,&ctx);
 fui::DeviceContext d;d.width=528;d.height=792;fui::InputSnapshot input;
 fui::InteractionBuffer<1>hits;fui::Frame<1>frame(t,d,input,hits);
 fui::DialogOption option{"Nho",1,0};fui::OptionDialogProps props;props.options=&option;props.optionCount=1;props.verticalOptions=true;
 props.buttonText.align=fui::TextAlign::Left;props.buttonHeight=56;
 fui::optionDialog(frame,{100,200,240,88},props);
 bool ok=r.tx>r.mx&&r.mx>=100&&r.ty>=0;
 printf("%s X3 SDK optionDialog text=%d mark=%d\n",ok?"GREEN":"RED",r.tx,r.mx);
 return ok?0:1;
}
'''
file=a.output/'menu.cpp';file.write_text(cpp);exe=a.output/'menu';sdk=root/'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++','-std=c++17','-I'+str(sdk/'include'),str(file),str(sdk/'src/FreeInkUI.cpp'),'-o',str(exe)],check=True)
raise SystemExit(subprocess.run([str(exe)]).returncode)
