"""Execute X4 production layout/action routing with the real SDK and host draw boundary."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--expect-preview-hit', action='store_true')
parser.add_argument('--mutate-preview-hit', action='store_true')
parser.add_argument('--signature', action='store_true')
parser.add_argument('--expect-fade', action='store_true')
parser.add_argument('--expect-stepper-ring', action='store_true')
parser.add_argument('--expect-middle-font', action='store_true')
parser.add_argument('--mutate-stepper-ring', action='store_true')
parser.add_argument('--mutate-middle-font', action='store_true')
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
if a.mutate_stepper_ring:
    source, removed = re.subn(
        r'\s*tenorchrome::drawPillRing\(\*renderer_, minusRect\.x.*?true\);\n'
        r'\s*tenorchrome::drawPillRing\(\*renderer_, plusRect\.x.*?true\);',
        '', source, flags=re.S)
    if removed != 1:
        raise SystemExit(f'stepper ring mutation expected 1 match, got {removed}')
if a.mutate_middle_font:
    source, removed = re.subn(r'if \(fonts && !windowLabels_\[i\]\.empty\(\)\)',
                              'if (false && fonts && !windowLabels_[i].empty())', source, count=1)
    if removed != 1:
        raise SystemExit('middle font mutation expected one guard')
if a.mutate_preview_hit:
    source, removed = re.subn(
        r'^\s*screen\.frame\(\)\.hit\(\{rowsRect\.x, previewY, rowsRect\.width, previewHeight\}, ACTION_ROW,\n'
        r'\s*static_cast<int16_t>\(previewIndex\), listProps_\.inputMask\);\n',
        '          static_cast<void>(previewIndex);\n', source, flags=re.M)
    if removed != 1:
        raise SystemExit(f'preview hit mutation expected 1 match, got {removed}')
header = (a.repo / 'src/activities/reader/ReaderToolbarUi.h').read_text()
header = re.sub(r'^#(?:include|pragma).*\n', '', header, flags=re.M)
chrome = (a.repo / 'src/components/TenorMenuChrome.h').read_text()
constants = '\n'.join(re.findall(r'^constexpr fui::ActionId .*?;', source, re.M)).replace('constexpr', '[[maybe_unused]] constexpr')
cpp = r'''
#include <FreeInkApp.h>
#include "Utf8.h"
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
constexpr bool kExpectPreviewHit = EXPECT_PREVIEW_HIT;
constexpr bool kDumpSignature = DUMP_SIGNATURE;
constexpr bool kExpectFade = EXPECT_FADE;
constexpr bool kExpectStepperRing = EXPECT_STEPPER_RING;
constexpr bool kExpectMiddleFont = EXPECT_MIDDLE_FONT;
std::string rectSignature(fui::Rect r) {
  return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.width) + "," +
         std::to_string(r.height);
}
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
  std::vector<std::string> commands;
  struct Text { fui::Rect rect; std::string label; bool bold; };
  std::vector<Text> texts;
  fui::Size measureText(fui::FontId,const char* text,fui::TextStyle) const override {
    return {static_cast<int16_t>(strlen(text)*(14+tier*2)), static_cast<int16_t>(24+tier*8)};
  }
  int16_t lineHeight(fui::FontId) const override { return static_cast<int16_t>(33+tier*5); }
  void fill(fui::Rect r,fui::Paint,uint8_t=0,uint8_t=fui::CornersAll) override { commands.push_back("fill:" + rectSignature(r)); }
  void stroke(fui::Rect r,fui::Paint,uint8_t,uint8_t radius=0,uint8_t=fui::CornersAll) override {
    commands.push_back("stroke:" + rectSignature(r) + ":" + std::to_string(radius));
    if(radius==20) frames.push_back(r);
  }
  void line(fui::Point a,fui::Point b,uint8_t width,fui::Paint) override {
    commands.push_back("line:" + std::to_string(a.x) + "," + std::to_string(a.y) + "," +
                       std::to_string(b.x) + "," + std::to_string(b.y) + ":" + std::to_string(width));
  }
  void triangle(fui::Point a,fui::Point b,fui::Point c,fui::Paint) override {
    commands.push_back("triangle:" + std::to_string(a.x) + "," + std::to_string(a.y) + "," +
                       std::to_string(b.x) + "," + std::to_string(b.y) + "," + std::to_string(c.x) + "," +
                       std::to_string(c.y));
  }
  void text(fui::Rect rect,const char* label,fui::TextStyle style) override {
    if(label) { texts.push_back({rect,label,style.bold}); commands.push_back("text:" + rectSignature(rect) + ":" + label); }
  }
  void bitmap(fui::Rect r,fui::BitmapRef,fui::BitmapMode,fui::Paint=fui::Paint::solid(fui::Color::Black),fui::Rotation=fui::Rotation::None) override {
    commands.push_back("bitmap:" + rectSignature(r));
  }
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
std::vector<std::string> favoriteMarks;
void drawFavoriteMark(const GfxRenderer&,int x,int y,int) { favoriteMarks.push_back(std::to_string(x) + "," + std::to_string(y)); }
// The panels' one grey ring (TenorMenuChrome): x 16, the screen's width less 32.
std::vector<fui::Rect> panels;
void drawPanel(const GfxRenderer&,int y,int h) { panels.push_back({16,static_cast<int16_t>(y),448,static_cast<int16_t>(h)}); }
int footBackTop(int h) { return h-76; }
std::vector<std::string> rowRules;
void drawRowRule(const GfxRenderer&,int y,int left,int right) {
  rowRules.push_back(std::to_string(y) + "," + std::to_string(left) + "," + std::to_string(right));
}
std::vector<std::string> fades;
void fadeBand(const GfxRenderer&, int y0, int h, bool outerTop, int x0 = 0, int x1 = -1) {
  if (!outerTop) fades.push_back(std::to_string(y0) + "," + std::to_string(h) + "," + std::to_string(x0) + "," + std::to_string(x1));
}
std::vector<std::string> stepperRings;
void drawPillRing(const GfxRenderer&, int x, int y, int w, int h, int thick, bool grey) {
  stepperRings.push_back(std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                         std::to_string(h) + "," + std::to_string(thick) + "," + std::to_string(grey));
}
''' + '\n'.join(re.findall(r'^constexpr int (?:READER_BAR_LEFT|READER_TOOL_END_AIR|FRAME_BAR_WIDTH|FRAME_BAR_AIR) = .*?;', chrome, re.M)) + '\n' + method(chrome, 'struct FrameBar') + ';\n' + method(chrome, 'inline FrameBar frameScrollBar') + '\n' + method(chrome, 'struct ReaderToolRect') + ';\n' + method(chrome, 'inline ReaderToolRect readerToolRect') + '\n}\n'
cpp = cpp.replace('EXPECT_PREVIEW_HIT', str(a.expect_preview_hit).lower()).replace('DUMP_SIGNATURE', str(a.signature).lower()).replace('EXPECT_FADE', str(a.expect_fade).lower()).replace('EXPECT_STEPPER_RING', str(a.expect_stepper_ring).lower()).replace('EXPECT_MIDDLE_FONT', str(a.expect_middle_font).lower())
cpp += header + '\n' + constants + '\n'
cpp += 'ReaderToolbarUi::ReaderToolbarUi(GfxRenderer& r): UiAppHost(r), renderer_(&r) {}\n'
cpp += method(source, 'fui::Rect readerFrame') + '\n'
for name in ['begin','render','route','onAction','screenFn','scrollRows','buildX4Tools','buildX4Toolbar','buildX4Panel','buildX4Spacing','buildX4Keypad','fadeMoreBelow']:
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
    model.rowText=[](int i){return std::string(i==0?"Font":(i==4?"Chapter initial":"Setting"));};
    model.rowValue=[](int i){return std::string(i==0?"SP3 - Traveling Typewriter-BOLD2":(i==1?"18":"Default"));};
    tenorchrome::stepperRings.clear();
    ui.setModel(model); ui.render();
    auto frame=tenorchrome::panels.back(); assert(frame.x==16 && frame.y==362 && frame.width==448 && frame.height==350);
    for(int row=0;row<5;++row) minimum(ui,ACTION_ROW,row);
    for(int tool=0;tool<tenorchrome::READER_TOOLS;++tool) minimum(ui,ACTION_TOOL,tool);
    minimum(ui,ACTION_SIZE_STEP,-1); minimum(ui,ACTION_SIZE_STEP,1); minimum(ui,ACTION_SIZE_ENTRY,0);
    auto minus=tap(ui,ACTION_SIZE_STEP,-1); assert(minus.event==ReaderToolbarUi::Event::SizeStep && minus.value==-1);
    auto plus=tap(ui,ACTION_SIZE_STEP,1); assert(plus.event==ReaderToolbarUi::Event::SizeStep && plus.value==1);
    auto size=tap(ui,ACTION_SIZE_ENTRY,0); assert(size.event==ReaderToolbarUi::Event::SizeEntry);
    const auto minusRect = ui.app.publishedRect(ACTION_SIZE_STEP, -1);
    const auto plusRect = ui.app.publishedRect(ACTION_SIZE_STEP, 1);
    assert(minusRect.x == 260 && minusRect.y == 464 && minusRect.width == 60 && minusRect.height == 62);
    assert(plusRect.x == 388 && plusRect.y == 464 && plusRect.width == 60 && plusRect.height == 62);
    bool sawPlus = false;
    for (const auto& text : ui.uiTarget.texts) {
      if (text.label == "+") {
        sawPlus = true;
        assert(std::abs((text.rect.x + text.rect.width / 2) - (418)) <= 1 &&
               "plus label must stay centered on the pill");
      }
    }
    assert(sawPlus && "plus label must be drawn");
    if (kExpectStepperRing) {
      assert(tenorchrome::stepperRings.size() == 2 && "size stepper must draw 2 grey pill rings");
      assert(tenorchrome::stepperRings[0] == "264,473,52,44,2,1" &&
             tenorchrome::stepperRings[1] == "392,473,52,44,2,1");
      bool sawFont = false;
      for (const auto& text : ui.uiTarget.texts) {
        if (text.label == "Font") sawFont = true;
        if (text.label.find("SP3 - Traveling") != std::string::npos)
          assert(text.label.find("\xE2\x80\xA6") != std::string::npos && "long font value must use middle ellipsis");
      }
      assert(sawFont && "Font label must stay intact");
    }
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
    model.rowText=[](int i) {
      if (i == 0) return std::string("SP3 - Traveling Typewriter-BOLD1");
      if (i == 1) return std::string("SP3 - Traveling Typewriter-BOLD2");
      return std::string("Short family");
    };
    int fontMarked = 0;
    model.rowMarked=[&fontMarked](int i){return i==fontMarked;};
    ui.setModel(model); ui.nav().reset();
    ui.uiTarget.commands.clear(); ui.uiTarget.frames.clear(); ui.uiTarget.texts.clear();
    tenorchrome::panels.clear(); tenorchrome::favoriteMarks.clear(); tenorchrome::rowRules.clear();
    tenorchrome::fades.clear();
    ui.render();
    assert(ui.visibleRows()==4);
    for(int row=0;row<4;++row) minimum(ui,ACTION_ROW,row);
    const auto preview=ui.app.publishedRect(ACTION_ROW,4);
    if (kExpectPreviewHit) {
      assert(!preview.empty() && "font preview row must be selectable");
      assert(preview.x==16 && preview.y==650 && preview.width==436 && preview.height==38);
      auto previewEvent=tap(ui,ACTION_ROW,4);
      assert(previewEvent.event==ReaderToolbarUi::Event::Row && previewEvent.value==4);
    } else {
      assert(preview.empty() && "baseline must leave clipped preview without a hit");
    }
    if (kExpectFade) {
      assert(tenorchrome::fades.size() == 1 && "font preview must fade below the full rows");
      assert(tenorchrome::fades[0] == "650,38,16,452" &&
             "font preview fade must stop before the scroll bar");
    }
    if (kExpectMiddleFont) {
      int measureCalls = 0;
      const std::string longName = "0123456789012345678901234567890123456789";
      const auto measured = [&](const char* text) {
        ++measureCalls;
        return static_cast<int>(strlen(text) * 14);
      };
      const auto compactName = utf8MiddleEllipsis(longName, 200, measured);
      assert(compactName.find("\xE2\x80\xA6") != std::string::npos);
      assert(measureCalls <= 45 && "40-character ellipsis must measure at most 45 candidates");
      for (int selected : {0, 1}) {
        fontMarked = selected;
        ui.uiTarget.texts.clear();
        ui.setModel(model); ui.nav().reset(); ui.render();
        bool sawOne = false, sawTwo = false;
        for (const auto& text : ui.uiTarget.texts) {
          if (text.label.find("BOLD1") != std::string::npos) {
            sawOne = true;
            assert(text.label.find("\xE2\x80\xA6") != std::string::npos && text.bold == (selected == 0));
          }
          if (text.label.find("BOLD2") != std::string::npos) {
            sawTwo = true;
            assert(text.label.find("\xE2\x80\xA6") != std::string::npos && text.bold == (selected == 1));
          }
        }
        assert(sawOne && sawTwo && "both long font variants must retain their suffix");
      }
    }
    if (kDumpSignature) {
      std::printf("SIG tier=%d\n", tier);
      for (const auto& command : ui.uiTarget.commands) std::printf("C %s\n", command.c_str());
      for (const auto& frame : tenorchrome::panels) std::printf("P %s\n", rectSignature(frame).c_str());
      for (const auto& mark : tenorchrome::favoriteMarks) std::printf("M %s\n", mark.c_str());
      for (const auto& rule : tenorchrome::rowRules) std::printf("R %s\n", rule.c_str());
    }
    assert(!ui.app.interactionOverflowed());
  }
  puts("GREEN X4 production layout: 3 tiers, 5 rows, 12 release-only keys, spacing draft/commit, 4 font rows + peek, shared tools");
}
'''
(a.output / 'layout.cpp').write_text(cpp)
sdk=a.repo / 'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',
                '-I'+str(sdk/'include'),'-I'+str(a.repo/'lib/I18n'),'-I'+str(a.repo/'lib/Utf8'),'-I'+str(a.repo/'src'),str(a.output/'layout.cpp'),str(sdk/'src/FreeInkUI.cpp'),str(a.repo/'lib/Utf8/Utf8.cpp'),'-o',str(a.output/'layout')],check=True)
subprocess.run([str(a.output/'layout')],check=True)
