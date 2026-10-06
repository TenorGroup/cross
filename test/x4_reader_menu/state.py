"""Run production Epub menu input and commit lifecycle on deterministic host boundaries."""
import argparse
from pathlib import Path
import re
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2])
parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args();a.output.mkdir(parents=True,exist_ok=True)
def method(text,name):
    match=re.search(r'^[^\n]*EpubReaderActivity::'+name+r'\(',text,re.M)
    start=match.start();brace=text.index('{',match.end());end=brace+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
s=(a.repo/'src/activities/reader/EpubReaderActivity.cpp').read_text()
h=(a.repo/'src/activities/reader/ReaderToolbarUi.h').read_text()
events=re.search(r'enum class Event \{.*?;',h,re.S).group(0)
routed=re.search(r'  struct Routed \{.*?\n  };',h,re.S).group(0)
spacing=re.search(r'constexpr uint8_t kSpacingByPlace\[\].*?;',s,re.S).group(0)
cpp=r'''
#include <FreeInkApp.h>
#include <ReaderFontSizes.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#define FREEINK_DEVICE_X4PRO 1
struct RenderLock {};
struct Settings {
  int fontPointSize=18,lineSpacing=0,paragraphAlignment=0,dropCapMode=1,writes=0;
  char sdFontFamilyName[32]={};
  void saveToFile(){++writes;}
} SETTINGS;
namespace readerSpacing {
constexpr int LEVEL_COUNT=5,DROP_CAP_MODE_COUNT=3;
int clampLevel(int v){return std::clamp(v,0,4);}
}
struct CrossPointSettings { static constexpr int PARAGRAPH_ALIGNMENT_COUNT=5; };
''' + spacing+r'''
int textChoiceCount(int row) { return row==2?5:row==3?5:row==4?3:0; }
int textChoiceInUse(int row) {
  if(row==2){for(int i=0;i<5;++i)if(kSpacingByPlace[i]==SETTINGS.lineSpacing)return i;}
  return row==3?SETTINGS.paragraphAlignment:row==4?SETTINGS.dropCapMode:-1;
}
// The value a choice row stores and what each place stores (chooseTextValue); no harness row is a catalog one.
int& textChoiceValue(int row) { return row==2?SETTINGS.lineSpacing:row==3?SETTINGS.paragraphAlignment:SETTINGS.dropCapMode; }
int textChoiceStored(int row,int place) { return row==2?kSpacingByPlace[place]:place; }
const void* catalogTextRow(int) { return nullptr; }
namespace fontdoc {
struct Ho { const char* ten="Family"; };
int family=0;
std::vector<Ho> danhSachHo(SdCardFontRegistry*) {return std::vector<Ho>(7);}
int hoDangDung(SdCardFontRegistry*) {return family;}
int coDangDung(const std::vector<uint8_t>& sizes){return static_cast<int>(std::find(sizes.begin(),sizes.end(),snapToNearestPointSize(sizes,SETTINGS.fontPointSize))-sizes.begin());}
void apCo(int,uint8_t value){SETTINGS.fontPointSize=value;}
bool apHo(int,SdCardFontRegistry*,int index){family=index;if(index>1) snprintf(SETTINGS.sdFontFamilyName,32,"Pack");else SETTINGS.sdFontFamilyName[0]=0;return true;}
}
struct FontSystem {SdCardFontRegistry data;SdCardFontRegistry& registry(){return data;}void releaseCatalog(){};} ;
struct Manager {bool sleep=false;bool isSleepTransition(){return sleep;}void deferWrite(std::function<void()> f){f();}}activityManager;
namespace tenorchrome {int note=0;void noteReaderFootBar(bool open,bool,int){note=open;}}
class MappedInputManager {
 public:
 enum class Button {Back,Left,Right,Up,Down,Confirm};
 enum class SwipeDir {None,Up,Down};
 bool back=false,release=false;
 int rowDelta=0;
 bool wasReleased(Button b)const{return b==Button::Back&&back;}
 bool isPressed(Button)const{return false;}
 int getHeldTime()const{return 0;}
 bool hasTouch()const{return true;}
 bool wasScreenTouchReleased()const{return release;}
 void resetHomeButtonInput(){}
};
struct Renderer {
 int getScreenWidth()const{return 480;}int getScreenHeight()const{return 800;}
 void restoreBwBuffer(bool){}bool storeBwBuffer(){return true;}
 void displayBuffer(int){}
 operator int()const{return 0;}
};
struct HalDisplay {static constexpr int FAST_REFRESH=1;};
struct Toc {int spineIndex=0,level=1;std::string anchor;};
struct Epub {int getTocItemsCount(){return 10;}int getTocIndexForSpineIndex(int){return 0;}Toc getTocItem(int){return {};}int getSpineItemsCount(){return 10;}};
struct Popup {bool isActive()const{return false;}template<class F>void handleInput(MappedInputManager&,F){}void dismiss(){}};
struct ButtonNavigator {static int previousIndex(int i,int n){return (i+n-1)%n;}static int nextIndex(int i,int n){return (i+1)%n;}};
struct ReaderToolbarUi {
''' + events + '\n' + routed+r'''
 static constexpr int kChoiceStride=8;
 Routed next;
 freeink::ui::ListNav nav_;
 Routed route(const MappedInputManager&){auto v=next;next={};return v;}
 freeink::ui::ListNav& nav(){return nav_;}
 int visibleRows()const{return 5;}
 int sheetRows()const{return 5;}
 int scrollRows(const MappedInputManager& input,int)const{return input.rowDelta;}
 void begin(){}void closeRouting(){}
};
class EpubReaderActivity {
 public:
 enum class Overlay {None,Toolbar,Text,Contents,More,Favorites};
 enum class TextDepth:uint8_t {Rows,Fonts,Spacing,PointSize};
 Overlay overlay=Overlay::None;
 TextDepth textDepth=TextDepth::Rows;
 bool spacingDragging=false;
 int spacingDraftPermille=500;
 std::string pointSizeDraft;
 FontSystem sdFontSystem;
 Renderer renderer;
 MappedInputManager mappedInput;
 std::unique_ptr<ReaderToolbarUi> toolbarUi=std::make_unique<ReaderToolbarUi>();
 Epub epubValue;Epub* epub=&epubValue;
 Popup overlayPopup;
 std::vector<fontdoc::Ho> fontFamilies;
 std::vector<int> moreItems;
 int focusedTool=0,panelIndex=0,currentSpineIndex=0,nextPageNumber=0;
 bool panelCursorShown=false,panelHoldJumped=false,overlayPageStored=true,bwUnderSheet=false;
 std::string pendingAnchor;
 std::unique_ptr<int> section=std::make_unique<int>(1);
 int redraws=0,previews=0,requests=0;
 bool textSettingsDirty=false,paintDropped=false;
 std::atomic<uint8_t> textCloseFrame{0};
 static constexpr int PANEL_HOLD_MS=600,PANEL_HOLD_STEP=5;
 void requestUpdate(){++requests;}
 void clearDeferredReposition(){}
 void paintOverlayPopup(){}
 void settleOverlayRefresh(){}
 void pushOverlayRefresh(){}
 void renderOverlay(){++redraws;}
 void discardOverlayPage(){overlayPageStored=false;}
 bool xteinkClassPanel()const{return true;}
 void danLaiTrang(){++previews;}
 int levelSheetRows=0;
 void applyReaderTextSettingsLocked(){}
 void applyTextSettingLive(){requestUpdate();}
 void cycleTextRow(int row){if(row==3)chooseTextValue(row,(SETTINGS.paragraphAlignment+1)%5);if(row==4)chooseTextValue(row,(SETTINGS.dropCapMode+1)%3);}
 void activateMoreRow(int){}
 std::vector<uint8_t> favoriteRows;
 void activateFavoriteRow(int){}void togglePin(uint8_t){}uint8_t pinOfRow(int)const{return 0xFF;}
 void openOverlay(Overlay target){overlay=target;textDepth=TextDepth::Rows;spacingDragging=false;pointSizeDraft.clear();if(!toolbarUi)toolbarUi=std::make_unique<ReaderToolbarUi>();toolbarUi->nav().reset();tenorchrome::note=1;}
 void handleOverlayInput();void closeOverlayToPage();void enterFontLevel();void leaveFontLevel();void chooseFontFamily(int);
 void enterTextDepth(TextDepth);void openTextRow(int);void stepMenuPointSize(int);void applyMenuPointSize(uint8_t);uint8_t enteredPointSize()const;
 void chooseTextValue(int,int);void invalidateTextSettingsLocked();void panelClosedLocked(bool,bool);void flushTextSettingsLocked();void markClosedTextFrameUpLocked();
};
constexpr int kTextRowCount=5;
constexpr int kReaderTools=4;
'''
for name in ['handleOverlayInput','closeOverlayToPage','openTextRow','enterFontLevel','leaveFontLevel','chooseFontFamily','enterTextDepth','stepMenuPointSize','applyMenuPointSize','enteredPointSize','chooseTextValue','invalidateTextSettingsLocked','panelClosedLocked','flushTextSettingsLocked','markClosedTextFrameUpLocked']:
    cpp+=method(s,name)+'\n'
cpp+=r'''
void event(EpubReaderActivity& r,ReaderToolbarUi::Event e,int value=0,int permille=-1,bool release=true){
 r.mappedInput.release=release;r.toolbarUi->next={e,value,permille,true,240,500};r.handleOverlayInput();r.mappedInput.release=false;
}
void back(EpubReaderActivity& r){r.mappedInput.back=true;r.toolbarUi->next.routed=true;r.handleOverlayInput();r.mappedInput.back=false;}
int main(){
 using D=EpubReaderActivity::TextDepth;using O=EpubReaderActivity::Overlay;using E=ReaderToolbarUi::Event;
 EpubReaderActivity r;
 assert(r.overlay==O::None&&SETTINGS.writes==0);
 r.openOverlay(O::Toolbar);event(r,E::Tool,1);assert(r.overlay==O::Text&&r.textDepth==D::Rows);
 event(r,E::Row,0);assert(r.textDepth==D::Fonts&&r.fontFamilies.size()==7);
 event(r,E::Row,3);assert(fontdoc::family==3&&r.previews==1&&SETTINGS.writes==0);
 event(r,E::Row,3);assert(r.previews==1);
 back(r);assert(r.textDepth==D::Rows&&r.overlay==O::Text&&SETTINGS.writes==0);
 event(r,E::Row,2);assert(r.textDepth==D::Spacing&&r.spacingDraftPermille==500);
 event(r,E::SpacingDraft,0,990,false);assert(r.spacingDraftPermille==990&&r.previews==1&&SETTINGS.lineSpacing==0);
 const int draftPaints=r.redraws;event(r,E::SpacingDraft,0,990,false);assert(r.redraws==draftPaints&&r.previews==1);
 event(r,E::SpacingCommit,0,990,true);assert(SETTINGS.lineSpacing==4&&r.previews==2&&SETTINGS.writes==0);
 event(r,E::SpacingCommit,0,1000,true);assert(r.previews==2);
 event(r,E::SpacingDraft,0,10,false);assert(r.previews==2);
 // A hardware release outside the target retains the held draft and applies once.
 event(r,E::None,0,-1,true);assert(SETTINGS.lineSpacing==1&&r.previews==3);
 event(r,E::SpacingDraft,0,1000,false);back(r);assert(r.textDepth==D::Rows&&SETTINGS.lineSpacing==1&&r.previews==3);
 event(r,E::SizeStep,-1);assert(SETTINGS.fontPointSize==16&&r.previews==4);
 event(r,E::SizeEntry);assert(r.textDepth==D::PointSize);
 event(r,E::NumericKey,10);event(r,E::NumericKey,10);event(r,E::NumericKey,3);event(r,E::NumericKey,0);
 assert(r.pointSizeDraft=="30"&&SETTINGS.fontPointSize==16);
 event(r,E::NumericKey,11);assert(SETTINGS.fontPointSize==26&&r.textDepth==D::Rows&&r.previews==5&&SETTINGS.writes==0);
 event(r,E::SizeEntry);event(r,E::NumericKey,10);event(r,E::NumericKey,10);event(r,E::NumericKey,9);event(r,E::NumericKey,9);event(r,E::NumericKey,9);
 assert(r.enteredPointSize()==255);back(r);assert(SETTINGS.fontPointSize==26&&r.previews==5);
 back(r);assert(r.overlay==O::None&&tenorchrome::note==0&&SETTINGS.writes==0&&r.textCloseFrame==1);
 // Dropped frame keeps the write pending. Visible-page delivery releases exactly one write.
 assert(SETTINGS.writes==0);r.paintDropped=true;r.markClosedTextFrameUpLocked();assert(r.textCloseFrame==1);r.paintDropped=false;r.markClosedTextFrameUpLocked();assert(r.textCloseFrame==2);r.flushTextSettingsLocked();assert(SETTINGS.writes==1);r.flushTextSettingsLocked();assert(SETTINGS.writes==1);
 r.openOverlay(O::Text);event(r,E::SizeStep,1);assert(r.previews==5);back(r);r.flushTextSettingsLocked();assert(SETTINGS.writes==1);
 r.openOverlay(O::Text);event(r,E::SizeEntry);back(r);assert(r.overlay==O::Text&&r.textDepth==D::Rows);back(r);assert(r.overlay==O::None);
 r.openOverlay(O::Contents);r.panelCursorShown=true;r.panelIndex=0;r.toolbarUi->nav().visibleRows=5;r.toolbarUi->nav().top=0;r.mappedInput.rowDelta=2;event(r,E::None);assert(!r.panelCursorShown&&r.toolbarUi->nav().top==2);r.mappedInput.rowDelta=0;back(r);assert(r.overlay==O::None);
 r.openOverlay(O::More);back(r);assert(r.overlay==O::None);
 r.openOverlay(O::Toolbar);back(r);assert(r.overlay==O::None);
 puts("GREEN Epub production journey: font, spacing hold/release/off-target/cancel, size step/snap/overflow/cancel, early Back, 1 save after close");
}
'''
(a.output/'state.cpp').write_text(cpp)
sdk=a.repo/'freeink-sdk/libs/ui/FreeInkUI'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined',
 '-I'+str(a.repo/'test/x4_reader_menu/stubs'),'-I'+str(a.repo/'src'),'-I'+str(sdk/'include'),str(a.output/'state.cpp'),str(a.repo/'src/ReaderFontSizes.cpp'),str(sdk/'src/FreeInkUI.cpp'),'-o',str(a.output/'state')],check=True)
subprocess.run([str(a.output/'state')],check=True)
