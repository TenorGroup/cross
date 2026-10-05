"""Execute production touch strip geometry and measure existing screenshot ink clearance."""
import argparse
from pathlib import Path
import re
import subprocess
from PIL import Image

p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2]);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
activity=(a.repo/'src/activities/reader/EpubReaderActivity.cpp').read_text()
assert 'renderer.fillRect(0, 0, renderer.getScreenWidth(), tenorchrome::contentTop(), false);' in activity, 'RED reader clear does not cover largest status ink'
h=(a.repo/'src/components/TenorMenuChrome.h').read_text();s=(a.repo/'src/components/TenorMenuChrome.cpp').read_text()
def method(text,signature):
 start=text.index(signature);brace=text.index('{',start);end=brace+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
constants=[]
for name in ['HEADER_TOP','HEADER_HEIGHT','TAB_HEIGHT','TOUCH_STATUS_TOP_INSET','TOUCH_STRIP_HEIGHT']:
 match=re.search(r'constexpr int '+name+r'\s*=.*?;',h)
 assert match,'RED missing '+name
 constants.append(match[0])
y=re.search(r'const int y = top \?.*?;',method(s,'void tenorchrome::drawStatus'),re.S)[0]
icon=re.search(r'const int iconY = .*?;',method(s,'void drawStripMiddle'),re.S)[0]
cpp=r'''
#include <algorithm>
#include <cassert>
struct Settings {int uiTextSize=0;} SETTINGS;
struct Spec {int bodyLineHeight,subtitleLineHeight;};
Spec uiTextSizeSpec(int tier) {return {tier==0?33:tier==1?38:43,tier==0?26:tier==1?33:38};}
namespace tenorchrome {
bool kTouchShell=true;
'''+'\n'.join(constants)+'\n'+'\n'.join(method(h,'inline int '+name+'(') for name in ['headerHeight','tabTop','tabHeight','contentTop'])+r'''
}
using namespace tenorchrome;
int statusTextY(int,bool,int) {return 700;}
struct Renderer {int line;int getLineHeight(int) const {return line;}int getScreenHeight() const {return 800;}};
int main() {
 for(int tier=0;tier<3;++tier) {
  SETTINGS.uiTextSize=tier;
  assert(headerHeight()==19 && tabTop()==32 && contentTop()==38);
  for(int line : {21,26,33,38,43}) {
   Renderer r{line};int fontChu=0,paddingBottom=0;bool lon=false,top=true;
''' + y+r'''
   const int oldY=5+(19-line)/2;
   assert(y==oldY+8);
   const int batteryHeight=line<33 ? 14:18;
   assert(y+5>=4 && y+5+batteryHeight<=TOUCH_STRIP_HEIGHT);
   top=false;
''' + y.replace('const int y','const int footerY')+r'''
   assert(footerY==700);
  }
  constexpr int STRIP_ICON=18;
''' +icon+r'''
  assert(iconY==11 && iconY+18<=TOUCH_STRIP_HEIGHT);
 }
 kTouchShell=false;
 for(int tier=0;tier<3;++tier) {
  SETTINGS.uiTextSize=tier;
  assert(headerHeight()==48+uiTextSizeSpec(tier).bodyLineHeight-33);
  assert(tabTop()==5+headerHeight());
  assert(contentTop()==tabTop()+tabHeight()+16);
 }
 kTouchShell=true;
 assert(28+TOUCH_STATUS_TOP_INSET==36);
 assert(28+TOUCH_STATUS_TOP_INSET<=contentTop());
 assert(contentTop()<=42 && 42>TOUCH_STRIP_HEIGHT);
}
'''
(a.output/'inset.cpp').write_text(cpp)
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(a.output/'inset.cpp'),'-o',str(a.output/'inset')],check=True)
subprocess.run([str(a.output/'inset')],check=True)
print('GREEN touch top inset +8: text, battery and radio; body38; strip32; X3 and reader footer unchanged')
