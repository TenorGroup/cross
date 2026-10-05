"""Execute the production sheet geometry against the real SDK, all 4 rotations."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
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
try:
    sheet = method(source, 'void ReaderToolbarUi::buildSheet')
except ValueError:
    raise SystemExit('RED: production sheet has no footer reservation')
cpp = r'''
#include <FreeInkApp.h>
#include <cassert>
#include <algorithm>
namespace fui=freeink::ui;
struct Target : fui::DrawTarget {
 fui::Rect last{};
 fui::Size measureText(fui::FontId,const char*,fui::TextStyle) const override {return {8,24};}
 int16_t lineHeight(fui::FontId) const override {return 24;}
 void fill(fui::Rect r,fui::Paint,uint8_t=0,uint8_t=fui::CornersAll) override {last=r;}
 void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t=0,uint8_t=fui::CornersAll) override {}
 void line(fui::Point,fui::Point,uint8_t,fui::Paint) override {}
 void triangle(fui::Point,fui::Point,fui::Point,fui::Paint) override {}
 void text(fui::Rect,const char*,fui::TextStyle) override {}
 void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint=fui::Paint::solid(fui::Color::Black),fui::Rotation=fui::Rotation::None) override {}
};
struct ReaderToolbarUi {
 using UiScreen=fui::FreeInkApp<24,6>::ScreenType;
 struct Model {fui::Insets footerInsets{};} model_;
 void buildSheet(UiScreen&,const fui::SheetProps&,int16_t);
};
''' + sheet + r'''
int main() {
 for(int rot=0;rot<4;++rot) for(int reserve : {0,32,48,66}) for(int height : {180,300,400,600,900}) {
  fui::DeviceContext d; d.width=rot%2 ? 800:480; d.height=rot%2 ? 480:800;
  d.orientation=static_cast<fui::Orientation>(rot);
  Target target; fui::InputSnapshot input; fui::InteractionBuffer<24> interactions; fui::Frame<24> frame(target,d,input,interactions); fui::ThemeTokens theme;
  ReaderToolbarUi::UiScreen screen(frame,theme); ReaderToolbarUi ui;
  const auto n=static_cast<int16_t>(reserve);
  ui.model_.footerInsets=rot==0?fui::Insets{0,0,n,0}:rot==1?fui::Insets{0,0,0,n}:rot==2?fui::Insets{n,0,0,0}:fui::Insets{0,n,0,0};
  fui::SheetProps props; props.grabberWidth=0; props.grabberHeight=0; props.grabberInset=0; props.grabberMargin=0;
  ui.buildSheet(screen,props,static_cast<int16_t>(height));
  const auto body=screen.body(); const auto band=d.screen().inset(ui.model_.footerInsets);
  assert(body.x>=band.x && body.right()<=band.right());
  assert(body.y>=band.y && body.bottom()<=band.bottom());
  assert(body.height<=height);
  assert(target.last.x>=band.x && target.last.right()<=band.right());
  assert(target.last.bottom()<=band.bottom());
 }
}
'''
(a.output / 'sheet.cpp').write_text(cpp)
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(a.repo/'freeink-sdk/libs/ui/FreeInkUI/include'),str(a.output/'sheet.cpp'),'-o',str(a.output/'sheet')],check=True)
subprocess.run([str(a.output/'sheet')],check=True)
print('GREEN: real SDK sheet body clears physical footer in 4 rotations, 4 reserves, 5 sheet sizes including oversized')
activity = (a.repo / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
overlay = method(activity, 'void EpubReaderActivity::renderOverlay()')
assert overlay.count('toolbarUi->render();\n    drawMenuChrome();') == 1, 'RED: toolbar path omits chrome'
assert overlay.count('toolbarUi->render();\n  drawMenuChrome();') == 1, 'RED: panel path omits chrome'
try:
    chrome = method(activity, 'const auto drawMenuChrome = [this]()') + ';'
except ValueError:
    raise SystemExit('RED: reader overlay has no menu chrome')
cpp = r'''
#include <cassert>
#include <algorithm>
#include <string>
#include <vector>
struct Settings {bool hidden=false; bool globalStatusBarHidden() const {return hidden;}} SETTINGS;
struct Metrics {int buttonHintsHeight=48;};
struct UITheme {Metrics metrics; static UITheme& getInstance() {static UITheme t;return t;} const Metrics& getMetrics() const {return metrics;}};
struct GfxRenderer {
 enum class Orientation {Portrait,LandscapeClockwise,PortraitInverted,LandscapeCounterClockwise};
 Orientation orientation=Orientation::Portrait;
 struct Fill {int x,y,w,h;}; std::vector<Fill> fills;
 int getScreenWidth() const {return static_cast<int>(orientation)%2 ? 800:480;}
 int getScreenHeight() const {return static_cast<int>(orientation)%2 ? 480:800;}
 Orientation getOrientation() const {return orientation;}
 void setOrientation(Orientation o) {orientation=o;}
 void fillRect(int x,int y,int w,int h,bool) {fills.push_back({x,y,w,h});}
};
int status=0,hints=0,arrows=0;
namespace tenorchrome {[[maybe_unused]] constexpr int TOUCH_STRIP_HEIGHT=24;void drawStatus(GfxRenderer&) {++status;}}
enum {STR_BACK,STR_SELECT,STR_DIR_LEFT,STR_DIR_RIGHT,STR_DIR_UP,STR_DIR_DOWN};
const char* tr(int i) {static const char* text[]={"Back","Select","Left","Right","Up","Down"};return text[i];}
struct Input {
 GfxRenderer* renderer;
 struct Labels {const char *btn1,*btn2,*btn3,*btn4;};
 Labels mapLabels(const char* a,const char* b,const char* c,const char* d) {const auto orientation=renderer->getOrientation();
  const bool swap=orientation==GfxRenderer::Orientation::PortraitInverted || orientation==GfxRenderer::Orientation::LandscapeCounterClockwise;
  if(swap) std::swap(c,d);
  return {d,c,b,a};}
};
struct Gui {
 std::vector<std::string> labels;
 void drawButtonHints(GfxRenderer& r,const char* a,const char* b,const char* c,const char* d) {
  assert(r.orientation==GfxRenderer::Orientation::Portrait);++hints; labels={a,b,c,d};
 }
 void drawSideButtonHints(GfxRenderer&,const char* a,const char* b) {assert(std::string(a)=="^" && std::string(b)=="v");++arrows;}
} GUI;
struct Reader {
 enum class Overlay {Toolbar,Text,Contents,More};Overlay overlay=Overlay::Toolbar;
 GfxRenderer renderer; Input mappedInput{&renderer};
 void draw() {
''' + chrome + r'''
 drawMenuChrome();
 }
};
int main() {
 for(bool hidden : {false,true}) for(int rot=0;rot<4;++rot) for(auto overlay : {Reader::Overlay::Toolbar,Reader::Overlay::Text,Reader::Overlay::Contents,Reader::Overlay::More}) {
  Reader r; r.overlay=overlay; auto orientation=static_cast<GfxRenderer::Orientation>(rot);r.renderer.orientation=orientation;
  SETTINGS.hidden=hidden;status=hints=arrows=0;r.draw();
  assert(r.renderer.orientation==orientation);
  assert(r.renderer.fills.size()==(hidden ? 0:1));
#if FREEINK_DEVICE_X4PRO
  assert(status==(hidden ? 0:1) && hints==0 && arrows==0);
  if(!hidden) {auto f=r.renderer.fills.front();assert(f.x==0 && f.y==0 && f.w==r.renderer.getScreenWidth() && f.h==24);}
#else
  assert(hints==(hidden ? 0:1) && arrows==0 && status==0);
  if(!hidden) {
   auto f=r.renderer.fills.front();assert(f.x==0 && f.y==752 && f.w==480 && f.h==48);
   assert(GUI.labels[2]=="Select" && GUI.labels[3]=="Back");
   assert(GUI.labels[0]==(overlay==Reader::Overlay::Toolbar ? "Right":"Down"));
   assert(GUI.labels[1]==(overlay==Reader::Overlay::Toolbar ? "Left":"Up"));
  }
#endif
 }
}
'''
(a.output/'chrome.cpp').write_text(cpp)
for touch in [0,1]:
    binary=a.output/('chrome'+str(touch))
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DFREEINK_DEVICE_X4PRO='+str(touch),str(a.output/'chrome.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('GREEN: production chrome executes in 4 rotations, 4 menus, visible/hidden, mapped button order, X4 top status')
