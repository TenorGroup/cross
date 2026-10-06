"""Compile the production tab viewport binding against the real SDK ListNav."""
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / 'freeink-sdk/libs/ui/FreeInkUI'
source = (ROOT / 'src/activities/UiTabListActivity.cpp').read_text()
match = re.search(r'void UiTabListActivity::syncTabListViewport\([^\n]*\) \{.*?\n\}', source, re.S)
assert match, 'production tab viewport binding missing'

HARNESS = r'''
#include <FreeInkUI.h>
#include <cassert>
#include <cstdio>
#include <initializer_list>
namespace fui = freeink::ui;
namespace tenorchrome { bool kTouchShell = true; }
constexpr int TENOR_PILL_ROW_PADDING_Y = 7;
struct Metrics { int listRowHeight=52, listWithSubtitleRowHeight=72; };
struct UITheme { static UITheme& getInstance() { static UITheme t; return t; } Metrics getMetrics() { return {}; } };
struct UiScreen { fui::ThemeTokens tokens; fui::Rect band{0,0,480,180};
  const fui::ThemeTokens& theme() { return tokens; } fui::Rect body() { return band; } };
struct Input { bool touch=true; bool hasTouch() { return touch; } };
struct UiTabListActivity {
  Input mappedInput; fui::ListNav nav; int count=10;
  int listCount() { return count; }
  fui::ListNav& activeNav() { return nav; }
  void frameRows(fui::ListProps&) {} void reserveFixedMenuContent(UiScreen&) {}
  void reserveFavoriteHint(UiScreen&) {} void decoratePinnedRows(fui::ListProps&) {}
  void reserveMoreBelowChevron(UiScreen&,int,int) {} void clampAfterNav() {}
  void reserveRowFrame(UiScreen&,int) {} void keepUglyRows(const fui::ListProps&) {}
  void syncTabListViewport(UiScreen&,fui::ListProps&,bool);
};
'''
CASES = r'''
int main() {
  int scenarios=0;
  for (int count : {6,10}) { // Stats actions including Quotes, long Settings root.
    UiTabListActivity a; UiScreen s; fui::ListProps p;
    s.tokens.rowHeight=56; s.tokens.listRowGap=6; p.count=count;
    a.nav.visibleRows=2; a.nav.drawnRows=2; a.nav.drawnCount=count;
    a.nav.followOnBuild=false; a.nav.selected=1;
    auto sync=[&] { a.count=count; p.count=count; a.syncTabListViewport(s,p,false); };
    a.nav.requestScroll(2); sync(); assert(a.nav.top==2 && p.topIndex==2); ++scenarios;
    sync(); assert(a.nav.top==2); ++scenarios; // consumed exactly once
    a.nav.requestScroll(-2); sync(); assert(a.nav.top==0); ++scenarios;
    assert(p.selectedIndex==-1 && a.nav.selected==1); ++scenarios;
    a.nav.requestScroll(1); a.nav.requestScroll(1); sync(); assert(a.nav.top==2); ++scenarios;
    a.nav.requestScroll(-1); a.nav.requestScroll(1); sync(); assert(a.nav.top==2); ++scenarios;
    a.nav.requestScroll(100); sync(); assert(a.nav.top==count-2); ++scenarios;
    a.nav.requestScroll(-100); sync(); assert(a.nav.top==0); ++scenarios;
  }
  tenorchrome::kTouchShell=false;
  UiTabListActivity a; UiScreen s; fui::ListProps p; a.mappedInput.touch=false;
  p.count=10; a.nav.selected=8; a.nav.followOnBuild=true;
  a.syncTabListViewport(s,p,false); assert(a.nav.top==5 && p.selectedIndex==7 && a.nav.selected==8); ++scenarios;
  a.nav.selected=0; a.nav.followOnBuild=true;
  a.syncTabListViewport(s,p,false); assert(a.nav.top==0 && p.selectedIndex==-1 && a.nav.selected==0); ++scenarios;
  a.nav.selected=8; a.nav.top=5; a.nav.followOnBuild=true; p.count=3; a.count=3;
  a.syncTabListViewport(s,p,false); assert(a.nav.selected==3 && p.selectedIndex==2 && a.nav.top==0); ++scenarios;
  a.nav.selected=1; a.nav.followOnBuild=true; p.count=10; a.count=10;
  a.nav.requestScroll(2); a.syncTabListViewport(s,p,false);
  assert(a.nav.top==2 && !a.nav.followPending); ++scenarios;
  printf("PASS: %d production tab binding / real SDK scroll scenarios\n",scenarios);
}
'''

with tempfile.TemporaryDirectory(prefix='tabbed-scroll-') as temporary:
    folder = Path(temporary)
    (folder / 'test.cpp').write_text(HARNESS + match.group() + CASES)
    command = ['c++', '-std=c++20', '-UNDEBUG', '-I' + str(SDK / 'include'),
               str(folder / 'test.cpp'), str(SDK / 'src/FreeInkUI.cpp'), '-o', str(folder / 'test')]
    build = subprocess.run(command, capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(folder / 'test')], capture_output=True, text=True)
    artifact = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
    if artifact:
        output = Path(artifact); output.mkdir(parents=True, exist_ok=True)
        (output / 'result.json').write_text(json.dumps(dict(returncode=run.returncode, stdout=run.stdout,
                                                          stderr=run.stderr, actual_production_binding=True), indent=2)+'\n')
        (output / 'binding.cpp').write_text(match.group())
    assert run.returncode == 0, run.stdout + run.stderr
    print(run.stdout, end='')
