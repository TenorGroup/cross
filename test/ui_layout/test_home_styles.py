"""Build the production layout contract, including book-to-hit mapping and native cover fit."""
from pathlib import Path
import argparse
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mutation', action='store_true')
p.add_argument('--header', type=Path, help='layout header to test (default: the one in src)')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
include = a.output / 'include/components'
include.mkdir(parents=True, exist_ok=True)
header = (a.header or root / 'src/components/HomeRecentLayout.h').read_text()
# Each mutation removes one rule of the layout; the contract below must fail on every one.
MUTATIONS = {
    'shown-book': ('return at < shown ? at : at + 1;', 'return at;'),
    'page-step': ('first + 3 * direction', 'first + 1 * direction'),
    'shelf-step': ('f.shelfStep = (width - 48 - 128) / 2;', 'f.shelfStep = 152;'),
    'peek-gap': ('constexpr int gap = 16;', 'constexpr int gap = 0;'),
}
cpp = a.output / 'styles.cpp'
cpp.write_text(r'''
#include "components/HomeRecentLayout.h"
#include <cassert>
#include <cstdio>
#include <set>
int main() {
  int cases=0;
  // The shelf's page of the others: round the ring, never the shown book, 3 cells at most, no book twice.
  for(int books=0;books<9;++books) for(int shown=0;shown<std::max(1,books);++shown) for(int first=-8;first<=8;++first) {
    std::set<int> seen;
    for(int slot=0;slot<3;++slot) {
      const int b=homeShelfBook(shown,books,first,slot);
      if(slot<std::min(3,std::max(0,books-1))) { assert(b>=0 && b<books && b!=shown && seen.insert(b).second); }
      else assert(b==-1);
    }
  }
  // 7 books (RECENT_LIMIT): 2 pages of 3, a step of 3 each way returns; 5 books: the second page has 1 new book.
  for(int first:{0,3,6,-3}) { auto f=homeRecentLayout(true,2,480,52,716,38,26,21,33,0,7,298,450,first);
    assert(f.shelfCount==3); }
  assert(homeShelfBook(0,7,0,0)==1 && homeShelfBook(0,7,3,0)==4 && homeShelfBook(0,7,3,2)==6 && homeShelfBook(0,7,6,0)==1);
  assert(homeShelfBook(0,5,0,2)==3 && homeShelfBook(0,5,3,0)==4 && homeShelfBook(0,5,3,1)==1);
  // A page step moves 3 along the ring of the others, both ways, and returns after a full round.
  for(int books=2;books<9;++books) for(int first=0;first<std::max(1,books-1);++first) {
    const int others=books-1, after=homeShelfPage(first,books,1), back=homeShelfPage(after,books,-1);
    if(others<=3) assert(after==first && back==first);
    else { assert(after==(first+3)%others && back==first && homeShelfPage(first,books,-1)==((first-3)%others+others)%others); }
    int at=first; for(int i=0;i<others;++i) at=homeShelfPage(at,books,1);
    assert(at==first || others<=3);
  }
  // X3 (528 px, body from y = 16): the 3 cells keep 24 px from both edges and share the rest; the Large cover is 261 wide.
  for(int tier=0;tier<3;++tier) for(int style=1;style<3;++style) for(auto size:{std::pair{298,450},std::pair{1200,800},std::pair{0,0}}) {
    const int title[]={38,43,43},body[]={26,33,38},caption[]={21,26,33};
    auto f=homeRecentLayout(true,style,528,16,700,title[tier],body[tier],caption[tier],33+tier*5,0,7,size.first,size.second);
    if(style==2) {
      assert(f.shelfStep==176 && f.shelfCount==3);
      for(int i=0;i<3;++i) { assert(f.shelfCellX(i)==24+176*i); assert(std::abs(2*(f.shelf[i].x-f.shelfCellX(i))+f.shelf[i].w-128)<=1); }
      assert(f.shelfCellX(2)+128==528-24);
    } else {
      assert(f.cover.w<=kHomeLargeCoverW && f.cover.y==16 && std::abs(2*f.cover.x+f.cover.w-528)<=1);
      assert(f.snapshot.x==0 && f.snapshot.w==528);
      for(bool left:{true,false}) for(auto n:{std::pair{298,450},std::pair{1200,800},std::pair{0,0}}) {
        auto r=homePeekRect(528,f.cover,left,n.first,n.second);
        assert(r.y+r.h==f.cover.y+f.cover.h && r.h<=f.cover.h && r.w<=kHomeLargeCoverW);
        if(left) assert(f.cover.x-(r.x+r.w)==16); else assert(528-r.x==f.cover.x-16 && r.x-(f.cover.x+f.cover.w)>=16);
      }
    }
    ++cases;
  }
  { // 261 x 408 on a 480 px screen: 93 px of each neighbour stands out, 16 px clear of the main cover
    auto f=homeRecentLayout(true,1,480,44,716,38,26,21,33,0,7,261,408);
    assert(f.cover.w==261 && f.cover.h==408 && f.cover.x==109);
    auto l=homePeekRect(480,f.cover,true,261,408), r=homePeekRect(480,f.cover,false,261,408);
    assert(l.x+l.w==93 && 480-r.x==93);
  }
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
          assert(f.shelfStep==152 && std::abs(2*(r.x-24-i*152)+r.w-128)<=1);
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


def run(text):
    (include / 'HomeRecentLayout.h').write_text(text)
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                    '-I'+str(a.output/'include'), '-I'+str(root/'src'), str(cpp), '-o', str(exe)], check=True)
    return subprocess.run([str(exe)], capture_output=True, text=True)


if a.mutation:
    for name, (old, new) in MUTATIONS.items():
        assert old in header, name
        assert run(header.replace(old, new)).returncode != 0, f'{name} mutation survived'
        print(f'GREEN: {name} mutation rejected')
else:
    result = run(header)
    print(result.stdout, end='')
    result.check_returncode()
