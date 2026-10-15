from pathlib import Path
import argparse
import subprocess

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--work', type=Path, required=True)
p.add_argument('--root', type=Path, default=ROOT)
p.add_argument('--source-ref')
p.add_argument('--touch', action='store_true')
a = p.parse_args()
path = 'src/components/TenorMenuChrome.cpp'
source = (subprocess.check_output(['git', 'show', f'{a.source_ref}:{path}'], cwd=a.root, text=True)
          if a.source_ref else (a.root / path).read_text())

def function(name):
    start = source.rfind('\n', 0, source.index(name)) + 1
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <algorithm>
#include <cstdio>
#include <vector>
struct GfxRenderer {
  mutable std::vector<unsigned char> pixels;
  int width, height;
  GfxRenderer(int w,int h): pixels(w*h),width(w),height(h) {}
  void drawPixel(int x,int y,bool) const {
    if(x>=0 && y>=0 && x<width && y<height) pixels[y*width+x]=1;
  }
};
namespace tenorchrome {
constexpr bool kTouchShell = TOUCH;
void drawRoundRing(const GfxRenderer&,int,int,int,int,int,int,bool);
void drawPillRing(const GfxRenderer&,int,int,int,int,int,bool);
}
FUNCTIONS
int main() {
  struct Size {int w,h; unsigned baseline;};
  const Size cases[]={{39,48,365172329u},{41,55,3673334067u},{42,60,357993025u},{48,48,1613654313u},
                      {139,48,1001255217u},{300,60,1837224321u}};
  int failures=0;
  for(const auto item:cases) {
    const int w=item.w,h=item.h;
    GfxRenderer ink(w,h),dotted(w,h),reference(w,h);
    tenorchrome::drawPillRing(ink,0,0,w,h,2,false);
    tenorchrome::drawPillRing(dotted,0,0,w,h,2,true);
    tenorchrome::drawRoundRing(reference,0,0,w,h,std::min(w,h)/2,2,false);
    int horizontal=0,vertical=0,pattern=0,count=0;
    unsigned hash=2166136261u;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
      const auto pixel=ink.pixels[y*w+x];
      horizontal += pixel!=ink.pixels[y*w+w-1-x];
      vertical += pixel!=ink.pixels[(h-1-y)*w+x];
      pattern += dotted.pixels[y*w+x] != (pixel && !((x+y)&1));
      count += pixel;
      hash=(hash^pixel)*16777619u;
    }
    const bool preserve=tenorchrome::kTouchShell || w>=h;
    const bool shape=preserve ? hash==item.baseline : ink.pixels==reference.pixels;
    const bool ok=(preserve || (!horizontal && !vertical)) && !pattern && count>0 && shape;
    std::printf("%s w=%d h=%d horizontal=%d vertical=%d pattern=%d shape=%d\n",
                ok?"GREEN":"RED",w,h,horizontal,vertical,pattern,shape);
    failures += !ok;
  }
  return failures?1:0;
}
'''
parts = ['bool inPill(', 'bool inRound(', 'void tenorchrome::drawRoundRing(', 'void tenorchrome::drawPillRing(']
code = code.replace('TOUCH', 'true' if a.touch else 'false')
code = code.replace('FUNCTIONS', '\n'.join(function(name) for name in parts))
a.work.mkdir(parents=True, exist_ok=True)
(a.work/'ring.cpp').write_text(code)
subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(a.work/'ring.cpp'),'-o',str(a.work/'ring')],check=True)
raise SystemExit(subprocess.run([str(a.work/'ring')]).returncode)
