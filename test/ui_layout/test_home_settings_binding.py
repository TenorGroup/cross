"""Run Home's production grouped paint methods against the actual FreeInkUI SDK."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / 'freeink-sdk/libs/ui/FreeInkUI'
home = (ROOT / 'src/activities/home/HomeActivity.cpp').read_text()
methods = []
for name in ('settingsRow', 'buildSettingsGroups', 'focusFavorite'):
    method = re.search(r'(?:void|bool|int) HomeActivity::' + name + r'\([^\n]*\) \{.*?\n\}', home, re.S)
    assert method, name
    methods.append(method.group())
# The row frame rule Home's touch groups share with every framed list (UiListActivity), as built.
lists = (ROOT / 'src/activities/UiListActivity.cpp').read_text()
for pattern in (r'UiListActivity::RowFrameLines UiListActivity::rowFrameLines\([^\n]*\) \{.*?\n\}',
                r'void UiListActivity::drawRowRule\([^\n]*\) \{.*?\n\}'):
    method = re.search(pattern, lists, re.S)
    assert method, pattern
    methods.insert(0, method.group())

HARNESS = r'''
#include "RecordingTarget.h"
#include <FreeInkApp.h>
#include <string>
#include "components/HomeSettingsLayout.h"
using UiScreen = fui::Screen<32>;
struct GfxRenderer {
  enum class Orientation { Portrait, LandscapeClockwise };
  Orientation orientation=Orientation::Portrait;
  std::vector<fui::Rect> frames;
  Orientation getOrientation() { return orientation; }
  mutable int rulePixels=0;
  void drawPixel(int,int,bool) const { ++rulePixels; }
  void drawRoundedRect(int x,int y,int w,int h,int,int,bool) {
    frames.push_back({static_cast<int16_t>(x),static_cast<int16_t>(y),static_cast<int16_t>(w),static_cast<int16_t>(h)});
  }
};
struct Settings { uint8_t uiTextSize=0; } SETTINGS;
namespace tenorchrome { bool kTouchShell=false; }
// The ugly shell's touch Home keeps these 2 frames; the Tenor touch shell lists 3 titled groups instead.
namespace infoupdate { bool shown() { return false; } }
I18n& I18n::getInstance() { static I18n i18n; return i18n; }
const char* I18n::get(StrId) const { return "heading"; }
const fui::TextStyle& uiMenuLabelText(const fui::ThemeTokens& theme) { return theme.bodyText; }
struct UiListActivity {
  int focusFavorite(const std::string&) { return -2; }
  struct RowFrameLines { int rule, top, bottom; };
  static RowFrameLines rowFrameLines(int rowGap);
  static void drawRowRule(const GfxRenderer& renderer, int y, int x0, int x1);
};
struct RenderLock { template<class T> RenderLock(T&) {} };
struct HomeActivity : UiListActivity {
  enum class Tab { CAI_DAT, OTHER }; Tab activeTabId=Tab::CAI_DAT;
  bool pinned=false; std::string settingsTransferLabel;
  GfxRenderer renderer; homesettings::Order settingsOrder; std::vector<fui::ListItem> rowItems;
  fui::ListNav nav; static constexpr fui::ActionId ACTION_ROW=42;
  void reserveFixedMenuContent(UiScreen&) {}
  void clampAfterNav() { nav.selected=std::clamp(nav.selected.load(),0,settingsOrder.count); }
  fui::ListNav& activeNav() { return nav; }
  static void settingsRow(void*,uint16_t,fui::ListItem&);
  bool buildSettingsGroups(UiScreen&);
  bool rowIsPinned(int row) { return pinned && settingsOrder.original(row)==0; }
  struct RowFrameLines { int rule, top, bottom; };
  static RowFrameLines rowFrameLines(int gap) { const int rule=(gap+1)/2; return {rule,rule+1,gap-rule+2}; }
  static void drawRowRule(GfxRenderer&,int,int,int) {}
  int focusFavorite(const std::string&);
};
'''
CASES = r'''
int main() {
  int paints=0;
  for (bool touch : {false,true}) for (uint8_t tier : {0,1,2}) for (bool motion : {false,true}) {
    tenorchrome::kTouchShell=touch; SETTINGS.uiTextSize=tier;
    HomeActivity home;
    std::vector<int> ids{0,7,1,2,3,4,5,6}; if(motion) ids.insert(ids.begin()+4,8);
    std::reverse(ids.begin(),ids.end()); home.settingsOrder=homesettings::order(ids);
    home.rowItems.resize(home.settingsOrder.count);
    for (int i=0;i<home.settingsOrder.count;++i) {
      home.rowItems[i].label=i==0?"Truyền tệp":"Setting"; home.rowItems[i].opensNext=true;
    }
    Target target(tier); fui::DeviceContext device;
    const int tops[]={129,141,151}, footers[]={40,80,90}, touchFooters[]={84,91,96};
    device.width=touch?480:528; device.height=touch?800:792; device.hasTouch=touch;
    device.safeArea={static_cast<int16_t>(touch?38:tops[tier]),0,
                     static_cast<int16_t>(touch?touchFooters[tier]:footers[tier]),0};
    fui::ThemeTokens theme=fui::themeTokensForLineHeight(target.lineHeight(1)); theme.bodyText.font=1;
    fui::InteractionBuffer<32> hits; fui::InputSnapshot input;
    fui::Frame<32> frame(target,device,input,hits); UiScreen screen(frame,theme);
    if (touch && motion && tier==2) {
      assert(!home.buildSettingsGroups(screen));
      assert(hits.count()==0 && home.renderer.frames.empty());
      continue;
    }
    for (int selected=0;selected<=home.settingsOrder.count;++selected) {
      hits.clear(); home.renderer.frames.clear(); home.nav.selected=selected;
      home.nav.followOnBuild=true; home.nav.requestScroll(3);
      assert(home.buildSettingsGroups(screen));
      assert(home.renderer.frames.size()==2 && hits.count()==static_cast<size_t>(home.settingsOrder.count));
      // Touch: the grey rules between the rows of each group (founder 06/10/2026); buttons draw none.
      assert(touch ? home.renderer.rulePixels>0 : home.renderer.rulePixels==0);
      home.renderer.rulePixels=0;
      assert(home.nav.selected==selected && home.nav.top==0 && home.nav.pageRowsFor(home.settingsOrder.count)==home.settingsOrder.count);
      for (int i=0;i<home.settingsOrder.count;++i) {
        const auto& hit=hits.data()[i];
        assert(hit.value==i && hit.action==HomeActivity::ACTION_ROW);
        assert(hit.rect.bottom()<=device.height-device.safeArea.bottom);
        fui::ListItem row; HomeActivity::settingsRow(&home,i,row);
        assert(row.label==home.rowItems[home.settingsOrder.original(i)].label && row.actionValue==i);
      }
      ++paints;
    }
    const int transfer=home.settingsOrder.display(0);
    home.pinned=true;
    fui::ListItem pinned; HomeActivity::settingsRow(&home,transfer,pinned);
    assert(std::string(pinned.label)=="\xEE\x84\x8A"+std::string(home.rowItems[0].label));
    assert(home.buildSettingsGroups(screen));
    assert(home.focusFavorite("action/15")==transfer && home.nav.selected==transfer+1);
    assert(home.focusFavorite("other")==-2);
    home.renderer.orientation=GfxRenderer::Orientation::LandscapeClockwise;
    hits.clear(); home.renderer.frames.clear(); assert(!home.buildSettingsGroups(screen));
    assert(hits.count()==0 && home.renderer.frames.empty());
    home.renderer.orientation=GfxRenderer::Orientation::Portrait;
    screen.setContentMargin({0,0,static_cast<int16_t>(screen.body().height-80),0});
    assert(!home.buildSettingsGroups(screen)); assert(hits.count()==0 && home.renderer.frames.empty());
  }
  printf("PASS: %d actual Home grouped paints, one nav, full hit targets, orientation/height fallbacks\n",paints);
}
'''

with tempfile.TemporaryDirectory(prefix='home-settings-binding-') as temporary:
    folder = Path(temporary)
    (folder / 'test.cpp').write_text(HARNESS + '\n'.join(methods) + CASES)
    build = subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I' + str(ROOT / 'src'),
                            '-I' + str(ROOT / 'test/ui_layout'), '-I' + str(ROOT / 'lib/I18n'),
                            '-I' + str(SDK / 'include'), str(folder / 'test.cpp'),
                            str(SDK / 'src/FreeInkUI.cpp'), '-o', str(folder / 'test')], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(folder / 'test')], capture_output=True, text=True)
    if artifact := os.environ.get('CROSSPOINT_TEST_ARTIFACTS'):
        output = Path(artifact); output.mkdir(parents=True, exist_ok=True)
        (output / 'binding.cpp').write_text('\n'.join(methods))
        (output / 'result.log').write_text(run.stdout + run.stderr)
    assert run.returncode == 0, run.stdout + run.stderr
    print(run.stdout, end='')
