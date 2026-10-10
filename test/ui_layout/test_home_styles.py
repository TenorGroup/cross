"""Build the production layout contract, including book-to-hit mapping and native cover fit."""
from pathlib import Path
import argparse
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mutation', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
include = a.output / 'include/components'
include.mkdir(parents=True, exist_ok=True)
header = (root / 'src/components/HomeRecentLayout.h').read_text()
if a.mutation:
    header = header.replace('if (book != shown)', 'if (true)')
(include / 'HomeRecentLayout.h').write_text(header)
cpp = a.output / 'styles.cpp'
cpp.write_text(r'''
#include "components/HomeRecentLayout.h"
#include <cassert>
#include <cstdio>
int main() {
  int cases=0;
  for(int tier=0;tier<3;++tier) for(int style=0;style<3;++style)
    for(int count=0;count<6;++count) for(int shown=0;shown<std::max(1,count);++shown)
      for(auto size:{std::pair{298,450},std::pair{1200,800},std::pair{0,0}}) {
        const int title[]={38,43,43},body[]={26,33,38},caption[]={21,26,33};
        auto f=homeRecentLayout(true,style,480,52,716,title[tier],body[tier],caption[tier],33+tier*5,shown,count,size.first,size.second);
        assert(f.style==static_cast<HomeRecentStyle>(style));
        assert(f.shelfCount==(style==2?std::min(3,std::max(0,count-1)):0));
        for(int i=0;i<f.shelfCount;++i) {
          assert(f.shelfBooks[i]>=0 && f.shelfBooks[i]<count && f.shelfBooks[i]!=shown);
          auto r=f.shelf[i];
          assert(std::abs(2*(r.x-24-i*152)+r.w-128)<=1);
          assert(r.w<=128 && r.y+r.h+f.shelfLabelHeight<=708);
          assert(r.w>=60 && r.h>=60);
        }
        if(style>0) {
          assert(f.cover.w>0 && f.cover.h>0 && f.cover.y>=52);
          assert(f.cover.y+f.cover.h+8<=f.titleY || style==2);
          assert(f.progressY+f.progressHeight<=f.ruleY-8);
          assert(f.authorY+f.authorLines*body[tier]<=f.progressY-8);
          if(style==2) {
            assert(f.progressY+f.progressHeight+16<=f.excerptY);
            assert(f.excerptY+33+tier*5<=f.ruleY);
            for(int i=1;i<f.shelfCount;++i) assert(f.shelfBooks[i]>f.shelfBooks[i-1]);
          }
          if(style==1) assert(std::abs(2*f.cover.x+f.cover.w-480)<=1);
        }
        auto button=homeRecentLayout(false,style,528,52,716,38,26,21,33,shown,count,298,450);
        assert(button.style==HomeRecentStyle::Stats);
        ++cases;
      }
  std::printf("GREEN layout cases=%d\n",cases);
}
''')
exe = a.output / 'styles'
subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                '-I'+str(a.output/'include'), '-I'+str(root/'src'), str(cpp), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)])
if a.mutation:
    assert result.returncode != 0, 'shown-book mutation survived'
    print('GREEN: shown-book mutation rejected')
else:
    result.check_returncode()
