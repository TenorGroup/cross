"""Run the production menu ink placement with recorded text and mark bounds."""
import argparse
import re
import subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2])
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
text=(a.repo/'src/components/OptionPopup.h').read_text()
if 'static void drawChoice(' in text:
    start=text.index('static void drawChoice(')
    end=text.index('\n  }',start)+4
    method=text[start:end].replace('static void ','void ')
else:
    start=text.index('        const int room = w - 2 * TEXT_X')
    end=text.index('\n      }\n      if (r + 1',start)
    body=text[start:end].replace('ownedStrings[i].c_str()','caption')
    method='void drawChoice(const GfxRenderer& renderer,int font,int x,int ry,int w,int ROW,const char* caption,bool marked,bool chosen){constexpr int TEXT_X=16,MARK=24;\n'+body+'\n}'
cpp=r'''
#include <string>
#include <cstdio>
#include <cmath>
namespace EpdFontFamily {enum Style{REGULAR,BOLD};}
struct GfxRenderer {
 mutable int tx=0,ty=0,mx=-1,my=-1;mutable EpdFontFamily::Style style=EpdFontFamily::BOLD;
 int getTextInkTop(int,const char*,EpdFontFamily::Style)const{return 4;}
 int getTextInkBottom(int,const char*,EpdFontFamily::Style)const{return 27;}
 std::string truncatedText(int,const char* s,int,EpdFontFamily::Style)const{return s;}
 void drawText(int,int x,int y,const char*,bool,EpdFontFamily::Style st)const{tx=x;ty=y;style=st;}
};
struct Icon{const unsigned char* bits=nullptr;}icon_row_chosen_24;
namespace tenorchrome{void drawBarIcon(const GfxRenderer& r,const unsigned char*,int,int,int x,int y,bool){r.mx=x;r.my=y;}}
''' + method + r'''
int main(){int bad=0;for(int h:{56,60,64})for(bool chosen:{false,true}){
 GfxRenderer r;drawChoice(r,12,100,200,240,h,"Nho",true,chosen);
 bool pass=r.tx==152&&r.style==EpdFontFamily::REGULAR;
 if(chosen)pass=pass&&r.mx==116&&std::abs((r.my+11)-(r.ty+15))<=0.5;
 else pass=pass&&r.mx==-1;
 printf("%s row=%d chosen=%d text=(%d,%d) mark=(%d,%d) bold=%d\n",pass?"GREEN":"RED",h,chosen,r.tx,r.ty,r.mx,r.my,r.style);bad+=!pass;
}return bad?1:0;}
'''
(a.output/'popup.cpp').write_text(cpp)
subprocess.run(['c++','-std=c++17',str(a.output/'popup.cpp'),'-o',str(a.output/'popup')],check=True)
raise SystemExit(subprocess.run([str(a.output/'popup')]).returncode)
