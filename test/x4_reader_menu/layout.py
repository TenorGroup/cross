"""Execute X4 production layout/action routing with the real SDK and host draw boundary."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--output', type=Path, required=True)
a = parser.parse_args()
a.output.mkdir(parents=True, exist_ok=True)

def method(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

source = (a.repo / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
header = (a.repo / 'src/activities/reader/ReaderToolbarUi.h').read_text()
header = re.sub(r'^#(?:include|pragma).*\n', '', header, flags=re.M)
chrome = (a.repo / 'src/components/TenorMenuChrome.h').read_text()
constants = '\n'.join(re.findall(r'^constexpr fui::ActionId .*?;', source, re.M)).replace('constexpr', '[[maybe_unused]] constexpr')
cpp = r'''
#include <FreeInkApp.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#define FREEINK_DEVICE_X4PRO 1
namespace fui = freeink::ui;
namespace shell { bool enabled=false; bool isUgly() { return enabled; } }
namespace freeink { struct Icon {}; }
namespace freeink::ui { BitmapRef bitmapFromIcon(const freeink::Icon&) { return {}; }
struct GfxRendererTarget { static constexpr int FONT_LABEL=3; }; }
freeink::Icon icon_reader_back_24,icon_reader_next_24,icon_reader_tick_24;
#include <I18n.h>
#include "activities/reader/ReaderMenuLayout.h"
I18n& I18n::getInstance(){static I18n i;return i;}
const char* I18n::get(StrId)const{return "Done";}
class GfxRenderer {};
struct UiSpec { int bodyFontId=0; };
UiSpec uiScaleSpec() { return {}; }
struct Target : fui::DrawTarget {
  int tier=0;
  void setPaintingEnabled(bool value) { painting=value; }
  bool paintingEnabled() const { return painting; }
  bool painting=true;
  std::vector<fui::Rect> frames;
  struct Text { fui::Rect rect; std::string label; };
  std::vector<Text> texts;
  fui::Size measureText(fui::FontId,const char* text,fui::TextStyle) const override {
    return {static_cast<int16_t>(strlen(text)*(8+tier*2)), static_cast<int16_t>(24+tier*8)};
  }
  int16_t lineHeight(fui::FontId) const override { return static_cast<int16_t>(33+tier*5); }
  void fill(fui::Rect,fui::Paint,uint8_t=0,uint8_t=fui::CornersAll) override {}
  void stroke(fui::Rect r,fui::Paint,uint8_t,uint8_t radius=0,uint8_t=fui::CornersAll) override { if(radius==20) frames.push_back(r); }
  void line(fui::Point,fui::Point,uint8_t,fui::Paint) override {}
  void triangle(fui::Point,fui::Point,fui::Point,fui::Paint) override {}
  void text(fui::Rect rect,const char* label,fui::TextStyle) override { if(label) texts.push_back({rect,label}); }
  void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint=fui::Paint::solid(fui::Color::Black),fui::Rotation=fui::Rotation::None) override {}
};
class MappedInputManager { public: fui::InputSnapshot snap; };
class UiAppHost {
 public:
  using UiApp=freeink::ui::FreeInkApp<24,6>;
  using UiScreen=UiApp::ScreenType;
  Target uiTarget;
  UiApp app;
  static fui::DeviceContext device() {
    fui::DeviceContext d; d.width=480; d.height=800; d.hasTouch=true; d.minTouchSize=60; return d;
  }
  explicit UiAppHost(const GfxRenderer&) : app(uiTarget,device()) {}
  void resetUi() {}
  void renderUi() { app.render(); }
  struct Touch { fui::ActionEvent event; fui::InputSnapshot snap; bool routed; };
  Touch routeTouch(const MappedInputManager& input,bool,bool) { return {app.route(input.snap), input.snap,true}; }
  int swipeRows(const MappedInputManager&,const fui::ListNav&,int,fui::ActionId) const { return 0; }
};
namespace tenorchrome {
constexpr int FOOT_BACK_SIZE=60,FOOT_BACK_X=16,FOOT_PILL_GAP=8,READER_TOOLS=4;
constexpr bool kTouchShell=true;
[[maybe_unused]] constexpr int PANEL_RADIUS=20;
constexpr int FAVORITE_MARK=14;
int favoriteMarks=0;
void drawFavoriteMark(const GfxRenderer&,int,int,int) { ++favoriteMarks; }
// The panels' one grey ring (TenorMenuChrome): x 16, the screen's width less 32.
std::vector<fui::Rect> panels;
void drawPanel(const GfxRenderer&,int y,int h) { panels.push_back({16,static_cast<int16_t>(y),448,static_cast<int16_t>(h)}); }
int footBackTop(int h) { return h-76; }
void drawRowRule(const GfxRenderer&,int,int,int) {}
''' + '\n'.join(re.findall(r'^constexpr int (?:READER_BAR_LEFT|READER_TOOL_END_AIR|FRAME_BAR_WIDTH|FRAME_BAR_AIR) = .*?;', chrome, re.M)) + '\n' + method(chrome, 'struct FrameBar') + ';\n' + method(chrome, 'inline FrameBar frameScrollBar') + '\n' + method(chrome, 'struct ReaderToolRect') + ';\n' + method(chrome, 'inline ReaderToolRect readerToolRect') + '\n}\n'
cpp += header + '\n' + constants + '\n'
cpp += 'ReaderToolbarUi::ReaderToolbarUi(GfxRenderer& r): UiAppHost(r), renderer_(&r) {}\n'
cpp += method(source, 'fui::Rect readerFrame') + '\n'
for name in ['begin','render','route','onAction','screenFn','scrollRows','buildX4Tools','buildX4Toolbar','buildX4Panel','buildX4Spacing','buildX4Keypad']:
    # Static onAction has a void signature like the other production members.
    match = re.search(r'^[^\n]*ReaderToolbarUi::'+name+r'\(', source, re.M)
    cpp += method(source, match.group(0)) + '\n'
cpp += r'''
void ReaderToolbarUi::paintUgly() {}
void ReaderToolbarUi::buildToolbar(UiScreen& s) { buildX4Toolbar(s); }
void ReaderToolbarUi::buildPanel(UiScreen& s) { buildX4Panel(s); }
ReaderToolbarUi::Routed tap(ReaderToolbarUi& ui, fui::ActionId action, int value) {
  const auto rect=ui.app.publishedRect(action,value);
  assert(!rect.empty());
  MappedInputManager input;
  input.snap.touchX=rect.x+rect.width/2; input.snap.touchY=rect.y+rect.height/2;
  input.snap.touchPressed=true;
  auto down=ui.route(input); assert(down.event==ReaderToolbarUi::Event::None);
  input.snap.touchPressed=false; input.snap.touchHeld=true;
  auto held=ui.route(input); assert(held.event==ReaderToolbarUi::Event::None);
  input.snap.touchHeld=false; input.snap.touchReleased=true;
  return ui.route(input);
}
void minimum(ReaderToolbarUi& ui,fui::ActionId action,int value) {
  auto r=ui.app.publishedRect(action,value);
  assert(r.width>=60 && r.height>=60);
  assert(r.x>=0 && r.y>=0 && r.right()<=480 && r.bottom()<=800);
}
int main() {
  GfxRenderer renderer;
  for(int tier=0;tier<3;++tier) {
    ReaderToolbarUi ui(renderer); ui.uiTarget.tier=tier;
    ui.begin();
    ReaderToolbarUi::Model model;
    model.panel=true; model.textView=ReaderToolbarUi::TextView::Rows;
    model.panelTitle="Text"; model.itemCount=5;
    model.rowText=[](int i){return std::string(i==4?"Chapter initial":"Setting");};
    model.rowValue=[](int i){return std::string(i==1?"18":"Default");};
    ui.setModel(model); ui.render();
    auto frame=tenorchrome::panels.back(); assert(frame.x==16 && frame.y==362 && frame.width==448 && frame.height==350);
    for(int row=0;row<5;++row) minimum(ui,ACTION_ROW,row);
    for(int tool=0;tool<tenorchrome::READER_TOOLS;++tool) minimum(ui,ACTION_TOOL,tool);
    minimum(ui,ACTION_SIZE_STEP,-1); minimum(ui,ACTION_SIZE_STEP,1); minimum(ui,ACTION_SIZE_ENTRY,0);
    auto minus=tap(ui,ACTION_SIZE_STEP,-1); assert(minus.event==ReaderToolbarUi::Event::SizeStep && minus.value==-1);
    auto plus=tap(ui,ACTION_SIZE_STEP,1); assert(plus.event==ReaderToolbarUi::Event::SizeStep && plus.value==1);
    auto size=tap(ui,ACTION_SIZE_ENTRY,0); assert(size.event==ReaderToolbarUi::Event::SizeEntry);
    assert(!ui.app.interactionOverflowed());
    ui.uiTarget.texts.clear();
    model.textView=ReaderToolbarUi::TextView::PointSize;
    model.numericDraft="30"; model.numericHint="12-26 pt; Done: 26 pt";
    ui.setModel(model); ui.render();
    int numericLabels=0;
    for(const auto& text:ui.uiTarget.texts) {
      if(text.label=="30" || text.label==model.numericHint) {
        ++numericLabels;
        assert(text.rect.y>=402 && text.rect.bottom()<=454 && text.rect.height>=43);
      }
    }
    assert(numericLabels==2 && "numeric readout and caption must both be drawn");
    assert(ui.app.publishedRect(ACTION_TOOL,0).empty());
    assert(ui.app.publishedRect(ACTION_ROW,0).empty());
    for(int key=0;key<12;++key) {
      minimum(ui,ACTION_NUMERIC,key);
      auto event=tap(ui,ACTION_NUMERIC,key);
      assert(event.event==ReaderToolbarUi::Event::NumericKey && event.value==key);
    }
    assert(!ui.app.interactionOverflowed());
    model.textView=ReaderToolbarUi::TextView::Spacing;
    model.spacingLabel=[](int){return "Default";};
    ui.setModel(model); ui.render(); minimum(ui,ACTION_SPACING,0);
    auto r=ui.app.publishedRect(ACTION_SPACING,0);
    MappedInputManager drag;
    drag.snap.touchPressed=true; drag.snap.touchX=r.x; drag.snap.touchY=r.y+r.height/2;
    auto event=ui.route(drag); assert(event.event==ReaderToolbarUi::Event::None);
    drag.snap.touchPressed=false; drag.snap.touchHeld=true; drag.snap.touchX=r.right()-1;
    event=ui.route(drag); assert(event.event==ReaderToolbarUi::Event::SpacingDraft && event.permille==1000);
    drag.snap.touchHeld=false; drag.snap.touchReleased=true;
    event=ui.route(drag); assert(event.event==ReaderToolbarUi::Event::SpacingCommit && event.permille==1000);
    model.textView=ReaderToolbarUi::TextView::Fonts; model.itemCount=31;
    model.rowMarked=[](int i){return i==0;};
    ui.setModel(model); ui.nav().reset(); ui.render();
    assert(ui.visibleRows()==4);
    for(int row=0;row<4;++row) minimum(ui,ACTION_ROW,row);
    assert(ui.app.publishedRect(ACTION_ROW,4).empty());
    assert(!ui.app.interactionOverflowed());
  }
  puts("GREEN X4 production layout: 3 tiers, 5 rows, 12 release-only keys, spacing draft/commit, 4 font rows + peek, shared tools");
}
'''
(a.output / 'layout.cpp').write_text(cpp)
sdk=a.repo / 'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',
                '-I'+str(sdk/'include'),'-I'+str(a.repo/'lib/I18n'),'-I'+str(a.repo/'src'),str(a.output/'layout.cpp'),str(sdk/'src/FreeInkUI.cpp'),'-o',str(a.output/'layout')],check=True)
subprocess.run([str(a.output/'layout')],check=True)
