"""Exercise the common thumb and the production list measurement on every touch list."""
import argparse
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--mutate', choices=('private-bar', 'no-timer', 'missing-screen'))
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.output.mkdir(parents=True, exist_ok=True)
old_test = (Path(__file__).with_name('test_x3_page_scrollbar.py')).read_text()
prologue = old_test.split("cpp = r'''", 1)[1].split("'''", 1)[0]
cpp = prologue.replace('mutable int windows=0;',
    'mutable int windows=0; mutable fui::Rect window{};')
cpp = cpp.replace('void displayWindow(int,int,int,int)const{++windows;}',
    'void displayWindow(int x,int y,int w,int h)const{++windows;window={int16_t(x),int16_t(y),int16_t(w),int16_t(h)};}')
for name in ('PageScrollbarIdle.h', 'PageScrollbar.h'):
    text = (root / 'src/components' / name).read_text()
    if a.mutate == 'no-timer' and name == 'PageScrollbar.h':
        text = text.replace('if (!tenorchrome::kTouchShell || !available.load()) return false;', 'return false;')
    cpp += re.sub(r'^#(?:pragma|include)[^\n]*\n', '', text, flags=re.M)
parent = (root / 'src/activities/UiListActivity.cpp').read_text()
measurement = parent[parent.index('void UiListActivity::measurePageScrollbar('):parent.index('void UiListActivity::drawPageScrollbar()')]
cpp += r'''
struct UiScreen {
 Target target_;
 fui::Rect body()const{return {0,53,480,592};}
 Target& target(){return target_;}
 struct Frame {fui::AssetResolver* assets()const{return nullptr;}} frame_;
 Frame& frame(){return frame_;}
};
namespace shell {bool isUgly(){return false;}}
struct UiListActivity {
 bool rowsFramed=tenorchrome::kTouchShell, groups=false; int rowFrameGap=6, rowFrameFloor=650;
 struct Lines {int top=3,bottom=5;};
 Lines rowFrameLines(int,int=0)const{return{};}
 int rowFrameAir()const{return 0;}
 fui::ListNav nav;
 fui::ListNav& activeNav(){return nav;}
 int listCount()const{return 20;}
 bool rowStartsGroup(int row)const{return groups && row==10;}
 struct Region {int top=0,bottom=0,length=0,offset=0;} region;
 void setPageScrollRegion(int top,int bottom,int length,int offset){region={top,bottom,length,offset};}
 void measurePageScrollbar(UiScreen&,const fui::ListProps&);
};
'''
cpp += measurement
cpp += r'''
int main(){
 UiScreen screen; fui::ListProps props;
 fui::ListItem items[20]; for(auto& item:items)item.label="row";
 props.items=items;props.count=20;props.rowHeight=56;props.rowGap=6;
 props.rowInset=16;props.labelText.font=1;
 UiListActivity list;list.nav.top=3;list.nav.visibleRows=10;list.measurePageScrollbar(screen,props);
 if(!tenorchrome::kTouchShell){
  assert(list.region.top==53 && list.region.bottom==645);
  assert(list.region.length==1184 && list.region.offset==177);
  return 0;
 }
 assert(list.region.top==50 && list.region.bottom==650);
 assert(list.region.length==1242 && list.region.offset==186);
 GfxRenderer renderer; MappedInputManager input; auto& bar=PageScrollbar::instance();
 int owners[32]{};
 // Distinct owners represent Home tabs, nested settings, readers and stand-alone lists.
 for(auto& owner:owners){
  now+=3000;bar.activate(&owner);Target target;
  bar.beginPaint();bar.draw(renderer,target,50,650,1242,186,&owner);bar.endPaint();
  assert(target.fills.size()==2);
  assert(target.fills.back().x==469 && target.fills.back().width==6);
  now+=1999;assert(!bar.needsIdleUpdate(input));
  now+=1;assert(bar.needsIdleUpdate(input));
  const int windows=renderer.windows;assert(bar.renderIdleUpdate(renderer,input));
  assert(renderer.windows==windows+1);
  assert(renderer.window.x==469 && renderer.window.y==50);
  assert(renderer.window.width==6 && renderer.window.height==600);
  Target hidden;bar.repaint(renderer,hidden);assert(hidden.fills.empty());
  assert(!bar.needsIdleUpdate(input));
  Target redraw;bar.draw(renderer,redraw,50,650,1242,186,&owner);assert(redraw.fills.empty());
  Target moved;bar.draw(renderer,moved,50,650,1242,248,&owner);assert(moved.fills.size()==2);
 }
 Target anchored;bar.draw(renderer,anchored,50,650,1242,186,&owners,400);
 assert(anchored.fills.back().x==469);
}
'''
sdk = root / 'freeink-sdk/libs/ui/FreeInkUI'
for touch in (0, 1):
    source, binary = a.output / f'scroll-{touch}.cpp', a.output / f'scroll-{touch}'
    source.write_text(cpp.replace('TOUCH', str(touch)))
    subprocess.run(['c++', '-std=c++20', '-I' + str(sdk / 'include'), str(source),
                    str(sdk / 'src/FreeInkUI.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
for name in ('QuotesActivity', 'QuoteDetailActivity', 'QuoteTrimActivity'):
    text = (root / f'src/activities/home/{name}.cpp').read_text()
    if a.mutate == 'missing-screen' and name == 'QuoteTrimActivity':
        text = text.replace('PageScrollbar::instance().draw(', 'privateScrollbar(')
    assert 'PageScrollbar::instance().draw(' in text, name + ' bypasses the common timer'
for folder in ('src', 'lib'):
    for path in (root / folder).rglob('*'):
        if path.suffix not in ('.h', '.cpp'): continue
        text = path.read_text()
        if a.mutate == 'private-bar' and path.name == 'FileBrowserActivity.cpp':
            text += '\nfreeink::ui::drawListScrollIndicator(target, rect, 100, 20, 0, 6);\n'
        if path.name != 'PageScrollbar.h':
            assert not re.search(r'\b(?:drawListScrollIndicator|drawScrollbar)\s*\(', text), path
print('GREEN: production single-frame measurement, 32 owners, x 469-474, 2000 ms, 1 strip update, no private bars')
