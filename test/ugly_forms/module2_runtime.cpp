#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <I18n.h>
#include <GfxRenderer.h>
#include "ReaderFontSizes.h"
#include "ReaderFontChon.h"
#include "ReaderInkWeight.h"
#include "ReaderSpacing.h"
#include "MenuFavorites.h"
#include "shells/ugly/UglyQuestionSheet.h"
#include "shells/ugly/UglyInk.h"
#if FREEINK_DEVICE_X4PRO
inline constexpr int activeProfile=1;
#else
inline constexpr int activeProfile=0;
#endif
static_assert(activeProfile==MODULE2_EXPECTED_BOARD,"Requested device profile must match compiled branch");
#define LOG_ERR(...) ((void)0)
int lockDepth=0;bool saveUnderLock=false,fontOutsideLock=false;int fontLoads=0;
struct JsonDoc {struct Field {std::optional<uint8_t> value;bool isNull()const{return !value;}uint8_t operator|(uint8_t fallback)const{return value.value_or(fallback);}};std::optional<uint8_t> mode,hide,items;Field operator[](const char* key)const{return {std::string(key)=="readerStatusBarMode"?mode:std::string(key)=="hideReaderStatusBar"?hide:items};}};
struct CrossPointSettings {
#include "SettingsFields.inc"
 bool saveOk=true;int saves=0;bool saveToFile(){saveUnderLock|=lockDepth!=0;++saves;return saveOk;}
} SETTINGS;
#include "SettingInfo.inc"
#include "Catalog.inc"
namespace shell {bool ugly=true;bool isUgly(){return ugly;}}
struct RenderLock {struct TryTake{};RenderLock(){++lockDepth;}template<class T>explicit RenderLock(T&&){++lockDepth;}~RenderLock(){--lockDepth;}bool acquired()const{return true;}};
struct FontSystem {void ensureLoaded(GfxRenderer&){fontOutsideLock|=lockDepth==0;++fontLoads;SETTINGS.fontPointSize=snapToNearestPointSize(readerFontPointSizes(registry,SETTINGS.sdFontFamilyName),SETTINGS.fontPointSize);}const SdCardFontRegistry* registry=nullptr;} sdFontSystem;
namespace menucustom {const char* canonicalPinKey(const char* s){return s;}}
namespace freeink::ui {struct ListItem{std::string label;int16_t actionValue=0;};}
struct OptionPopup {bool active=false;std::function<void(int)> callback;bool isActive()const{return active;}template<class... Args>void show(StrId,const StrId*,int,int,Args... args){active=true;(set(args),...);}template<class... Args>void show(StrId,const std::vector<std::string>&,int,Args... args){active=true;(set(args),...);}template<class T>void set(T cb){callback=cb;}void pick(int n){active=false;callback(n);}};
struct ThemeMetrics {int topPadding=0,headerHeight=0,verticalSpacing=0,buttonHintsHeight=0;};
struct UITheme {static UITheme& getInstance(){static UITheme t;return t;}ThemeMetrics getMetrics(){return {};}};
namespace textsettings {struct PreviewLayout {};}
namespace tenorchrome {bool enabled(){return false;}int contentTopUnderTabs(){return 0;}}
struct UiScreen {};struct MenuNavigationState {};enum class HomeMenuItem{SETTINGS_MENU};
struct Base {struct Nav {int selected=0;bool followOnBuild=false;} nav;GfxRenderer& renderer;MappedInputManager& mappedInput;int refreshes=0,exits=0;std::function<void()> finishHook;struct App {void clearTapFlash(){}} app;
 Base(const char*,GfxRenderer& r,MappedInputManager& m):renderer(r),mappedInput(m){}void onEnter(){}Nav& activeNav(){return nav;}void requestUpdate(){++refreshes;}void finish(){++exits;if(finishHook)finishHook();}void onGoHome(HomeMenuItem){++exits;}bool backReleased(){return mappedInput.wasReleased(MappedInputManager::Button::Back);}bool confirmReleased(){return mappedInput.wasReleased(MappedInputManager::Button::Confirm);}int ringPos()const{return nav.selected;}void moveRingTo(int n){nav.selected=n;}void commitTabNavigation(){}void pollTilt(){} };
struct UiTabListActivity:Base {using Base::Base;std::array<Nav,4> tabNavs{};int tabCount()const{return 4;}};
class UiListActivity:public Base {public:using Base::Base;};
struct ReadingStatsStore {static uint32_t currentDay(){return 20261005;}};
#include "TextSettingsActivity.inc"
#include "StatusBarSettingsActivity.inc"
struct ActivityResult {};
struct SettingsActivity:Base {using Base::Base;std::atomic<bool> saveFailed{false};std::unique_ptr<StatusBarSettingsActivity> child;bool saveSettings(bool repaint=true);void openStatus();template<class CB>void startActivityForResult(std::unique_ptr<StatusBarSettingsActivity> a,CB cb){child=std::move(a);child->finishHook=[cb]{cb(ActivityResult{});};child->onEnter();}};
namespace ugly {
int width(const GfxRenderer&,Size size,const char* s){int n=0;for(const unsigned char* p=(const unsigned char*)s;*p;++p)if((*p&0xc0)!=0x80)++n;return n*(size==Size::S22?9:13);}
int text(const GfxRenderer& r,Size size,int x,int y,const char* s,bool){++r.texts;const int w=width(r,size,s);if(r.textCount<512){auto& t=r.textRuns[r.textCount++];t.x=x;t.y=y;t.w=w;snprintf(t.value,sizeof(t.value),"%s",s);}return w;}
int paragraph(const GfxRenderer& r,Size size,int,int,int w,int,const char* s,bool draw){int lines=std::max(1,(width(r,size,s)+w-1)/w);if(draw)r.texts+=lines;return lines;}
std::string fit(const GfxRenderer& r,Size size,const std::string& s,int w){std::string out=s;while(!out.empty()&&width(r,size,out.c_str())>w)out.pop_back();return out;}
void line(const GfxRenderer& r,int,int,int,int,uint32_t,int){++r.strokes;}
void circle(const GfxRenderer& r,Circle kind,const Box& b,int,int,int){++r.strokes;if(kind==Circle::Word&&r.circleCount<256)r.circles[r.circleCount++]={(b.x0+b.x1)/2,(b.y0+b.y1)/2};}
void underline(const GfxRenderer& r,int,int,int,uint32_t,int){++r.strokes;}
void statusBar(const GfxRenderer& r,const MappedInputManager&,Hints){++r.navBars;}
void topBar(const GfxRenderer& r,const char*){++r.topBars;}
void formTopBar(const GfxRenderer& r){++r.topBars;}
void arrow(const GfxRenderer& r,int,int,bool,int){++r.strokes;}
void ensureFonts(GfxRenderer&){}
#if FREEINK_DEVICE_X4PRO
#include "NavRow.inc"
#endif
}
void TextSettingsActivity::rebuildRowItems(){}
void TextSettingsActivity::updatePreviewGeometry(){}
void TextSettingsActivity::prepareFormQuip(int,int){}
void StatusBarSettingsActivity::prepareFormQuip(int,int){}
#include "Methods.inc"
using Sheet=ugly::QuestionSheet;using Tab=TextSettingsActivity::Tab;
int checks=0,failures=0;
void check(bool ok,const char* name){++checks;if(!ok){++failures;printf("FAIL %s\n",name);}}
void reset(){SETTINGS={};SETTINGS.fontPointSize=14;SETTINGS.screenMargin=5;SETTINGS.statusBarTitle=CrossPointSettings::CHAPTER_TITLE;SETTINGS.statusBarChapterPageCount=SETTINGS.statusBarBookProgressPercentage=1;SETTINGS.statusBarClock=CrossPointSettings::STATUS_BAR_CLOCK_RIGHT;SETTINGS.readerStatusBarMode=SETTINGS.statusBarItemsMode=CrossPointSettings::READER_STATUS_BAR_DEFAULT;saveUnderLock=false;fontOutsideLock=false;fontLoads=0;sdFontSystem.registry=nullptr;}
const Sheet::Intent back{Sheet::IntentKind::Back};
template<class A>void backRoute(A& a,bool ugly){if(ugly)a.applyFormIntent(back);else{a.mappedInput.back=true;a.handleButtons();}}
int main(){
 GfxRenderer r;MappedInputManager input;
#if FREEINK_DEVICE_X4PRO
 r.w=480;r.h=800;input.touch=true;
#endif
 for(bool ugly:{false,true}) {shell::ugly=ugly;
  {reset();SettingsActivity parent("Settings",r,input);parent.openStatus();backRoute(*parent.child,ugly);check(SETTINGS.saves==0&&parent.child->exits==1,"Status clean Back+parentreturn 0writes");check(parent.refreshes==1,"Parent callback refresh1");}
  {reset();SettingsActivity parent("Settings",r,input);parent.openStatus();parent.child->applyChosenValue(0,0);backRoute(*parent.child,ugly);check(SETTINGS.saves==1&&parent.child->exits==1&&parent.refreshes==1,"Status change+Back+parentreturn 1write");}
  {reset();SettingsActivity parent("Settings",r,input);parent.openStatus();SETTINGS.saveOk=false;parent.child->applyChosenValue(1,0);backRoute(*parent.child,ugly);check(SETTINGS.saves==2&&parent.child->exits==0&&parent.refreshes==0,"Status failed Back blocks parentreturn");SETTINGS.saveOk=true;backRoute(*parent.child,ugly);check(SETTINGS.saves==3&&parent.child->exits==1&&parent.refreshes==1,"Status retry success parentreturn 3total attempts");check(!saveUnderLock&&lockDepth==0,"Status parentjourney saves outside RenderLock");}
  {reset();TextSettingsActivity a(r,input,nullptr);a.onEnter();check(SETTINGS.saves==0,"Text entry 0writes");backRoute(a,ugly);check(SETTINGS.saves==0&&a.exits==1,"Text clean Back 0writes");}
  {reset();TextSettingsActivity a(r,input,nullptr);a.onEnter();check(a.applyChosenValue(Tab::Layout,0,1)&&SETTINGS.lineSpacing==1,"Text layout RAM applied");backRoute(a,ugly);check(SETTINGS.saves==1&&a.exits==1,"Text change+Back 1write");check(!saveUnderLock,"Text save after unlock");}
  {reset();TextSettingsActivity a(r,input,nullptr);a.onEnter();SETTINGS.saveOk=false;a.applyChosenValue(Tab::Style,4,1);check(SETTINGS.readerInkWeight==1&&a.saveFailed_.load()&&SETTINGS.saves==1,"Text failed save keeps RAM+error");backRoute(a,ugly);check(a.exits==0&&SETTINGS.saves==2&&SETTINGS.readerInkWeight==1,"Text failed Back retry blocks exit");SETTINGS.saveOk=true;backRoute(a,ugly);check(a.exits==1&&SETTINGS.saves==3&&!a.saveFailed_.load(),"Text successful Back retry exits");check(!saveUnderLock&&!fontOutsideLock&&lockDepth==0,"Text ink reload lock/save boundaries");}
  {reset();StatusBarSettingsActivity a(r,input);a.onEnter();backRoute(a,ugly);check(SETTINGS.saves==0&&a.exits==1,"Status clean Back 0writes");}
  {reset();StatusBarSettingsActivity a(r,input);a.onEnter();a.refreshes=0;a.activateIndex(0);backRoute(a,ugly);check(SETTINGS.statusBarTitle==CrossPointSettings::HIDE_TITLE&&SETTINGS.saves==1&&a.exits==1,"Status change+Back 1write");check(a.refreshes==1,"Status activate 1refresh");}
  {reset();StatusBarSettingsActivity a(r,input);a.onEnter();SETTINGS.saveOk=false;a.applyChosenValue(1,0);backRoute(a,ugly);check(SETTINGS.statusBarChapterPageCount==0&&SETTINGS.saves==2&&a.exits==0,"Status failure keeps RAM and Back blocks");SETTINGS.saveOk=true;backRoute(a,ugly);check(SETTINGS.saves==3&&a.exits==1&&!a.saveFailed_.load()&&!saveUnderLock,"Status Back retries after unlock");}
  {reset();TextSettingsActivity a(r,input,nullptr);a.onEnter();for(int global=2;global<14;++global){const auto* s=a.formSetting(global);auto row=a.formRow(&a,global);if(s->type==SettingType::VALUE)continue;SETTINGS.*s->valuePtr=row.count-1;const int prev=SETTINGS.saves;if(ugly)a.activateIndex(global);else if(global<9)a.confirmLayoutRow(global-2);else a.confirmStyleRow(global-9);if(a.optionPopup_.isActive())a.optionPopup_.pick(0);check(SETTINGS.*s->valuePtr==0&&SETTINGS.saves==prev+1,"Layout/Style actual count wraps last to0");}
   SETTINGS.readerInkWeight=255;auto ink=a.formRow(&a,13);check(ink.selected==0&&ink.count==4,"Ink clamps corrupt value to0");a.confirmStyleRow(4);check(SETTINGS.readerInkWeight==readerInk::next(255),"Ink cycle follows clamped actual next");
   auto margin=a.formRow(&a,7);check(margin.count==8&&margin.selected==0,"Margin actual range maps 5..40 step5");check(a.applyChosenValue(Tab::Layout,5,7)&&SETTINGS.screenMargin==40,"Margin option7 stores40");char label[32];a.formLabel(&a,7,7,label,sizeof(label));check(std::string(label)=="40","Margin option label actual40");const int saves=SETTINGS.saves;check(a.applyChosenValue(Tab::Layout,5,7)&&SETTINGS.saves==saves,"Text selected harmless");check(!a.applyChosenValue(Tab::Layout,5,8)&&!a.applyChosenValue(Tab::Style,-1,0),"Text invalid bounds harmless");}
  {reset();SdCardFontRegistry registry;registry.families={{"Bitmap",false,{9,13,17}},{"Vector",true,{}}};sdFontSystem.registry=&registry;TextSettingsActivity a(r,input,&registry);a.onEnter();check(a.fonts_.size()==4&&a.fonts_[2].name=="Bitmap","Family dynamic registry appended");check(a.applyChosenValue(Tab::Family,0,2)&&std::string(SETTINGS.sdFontFamilyName)=="Bitmap"&&a.sizes_.size()==3&&a.currentSizeIndex_==1&&SETTINGS.fontPointSize==13,"Family applies actual bitmap set and snaps nearest");check(a.applyChosenValue(Tab::Size,0,2)&&SETTINGS.fontPointSize==17,"Size option2 maps actual17pt");char label[32];a.formLabel(&a,1,2,label,sizeof(label));check(std::string(label)=="17 pt","Dynamic size label actual17pt");check(a.applyChosenValue(Tab::Family,0,3)&&a.sizes_.size()==15&&a.formRow(&a,1).kind==Sheet::Kind::Ruler,"Vector family 15sizes uses ruler");a.applyChosenValue(Tab::Size,0,0);check(SETTINGS.fontPointSize==8,"Vector index0 stores8pt");a.applyChosenValue(Tab::Family,0,1);check(SETTINGS.sdFontFamilyName[0]==0&&SETTINGS.fontFamily==1&&a.sizes_.size()==4,"Built-in family clears SD name and uses4sizes");check(!fontOutsideLock&&!saveUnderLock&&lockDepth==0,"Family/size reload locked saves unlocked");}
  {reset();SdCardFontRegistry registry;for(int i=0;i<7;++i)registry.families.push_back({"Font"+std::to_string(i),false,{10,12,14}});sdFontSystem.registry=&registry;TextSettingsActivity a(r,input,&registry);a.onEnter();const int size=SETTINGS.fontPointSize,family=SETTINGS.fontFamily;int focus=a.focusFavorite("text/fontFamily");a.activateIndex(focus);check(SETTINGS.saves==0&&SETTINGS.fontFamily==family&&SETTINGS.fontPointSize==size,"Family favorite focus+activate leaves current value");if(ugly)check(a.form_.paperOpen(),"Long Family favorite opens chooser");if(ugly)a.form_.input(Sheet::Key::Back);focus=a.focusFavorite("text/fontSize");a.activateIndex(focus);check(SETTINGS.saves==0&&SETTINGS.fontPointSize==size,"Size favorite focus+activate leaves current value");focus=a.focusFavorite("text/lineSpacing");a.activateIndex(focus);check(SETTINGS.lineSpacing==1&&SETTINGS.saves==1,"Layout favorite still applies");focus=a.focusFavorite("text/readerInkWeight");a.activateIndex(focus);check(SETTINGS.readerInkWeight==1&&SETTINGS.saves==2,"Style favorite still applies");}
  {reset();SETTINGS.statusBarClock=0;StatusBarSettingsActivity a(r,input);a.onEnter();check(a.formRow(&a,3).selected==0,"Legacy clock0 displays right layout");a.applyChosenValue(3,0);check(SETTINGS.statusBarClock==CrossPointSettings::STATUS_BAR_CLOCK_RIGHT&&SETTINGS.saves==1,"Explicit legacy clock0 migrates to RIGHT");a.applyChosenValue(3,0);check(SETTINGS.saves==1,"Status explicit selected harmless");a.applyChosenValue(3,1);check(SETTINGS.statusBarClock==CrossPointSettings::STATUS_BAR_CLOCK_LEFT&&SETTINGS.saves==2,"Clock index1 maps LEFT");check(!a.applyChosenValue(-1,0)&&!a.applyChosenValue(4,0)&&!a.applyChosenValue(0,2)&&SETTINGS.saves==2,"Status invalid bounds harmless");}
  {reset();SETTINGS.statusBarProgressBar=SETTINGS.statusBarProgressBarThickness=SETTINGS.statusBarTitle=SETTINGS.xtcStatusBarMode=SETTINGS.statusBarClock=255;StatusBarSettingsActivity a(r,input);a.onEnter();check(SETTINGS.statusBarProgressBar==CrossPointSettings::HIDE_PROGRESS&&SETTINGS.statusBarProgressBarThickness==CrossPointSettings::PROGRESS_BAR_NORMAL&&SETTINGS.statusBarTitle==CrossPointSettings::HIDE_TITLE&&SETTINGS.xtcStatusBarMode==CrossPointSettings::XTC_STATUS_BAR_HIDE&&SETTINGS.statusBarClock==CrossPointSettings::STATUS_BAR_CLOCK_HIDE&&SETTINGS.saves==0,"Status entry clamps corrupt values without writes");}
 }
 for(int mode=0;mode<6;++mode){reset();SETTINGS.readerStatusBarMode=mode;SETTINGS.statusBarItemsMode=255;const bool title=mode>=2,pages=mode==2||mode==3,percent=mode==2;StatusBarSettingsActivity a(r,input);a.onEnter();check(SETTINGS.statusBarItemsMode==mode&&SETTINGS.saves==0,"Status adopts changed mode without writes");if(mode)check((SETTINGS.statusBarTitle!=CrossPointSettings::HIDE_TITLE)==title&&bool(SETTINGS.statusBarChapterPageCount)==pages&&bool(SETTINGS.statusBarBookProgressPercentage)==percent,"Status adopts actual preset switches");}
 {reset();SETTINGS.statusBarTitle=CrossPointSettings::HIDE_TITLE;SETTINGS.statusBarChapterPageCount=0;StatusBarSettingsActivity a(r,input);a.onEnter();check(SETTINGS.statusBarTitle==CrossPointSettings::HIDE_TITLE&&SETTINGS.statusBarChapterPageCount==0,"Matching mode preserves custom switches");}
 {reset();SETTINGS.readerStatusBarMode=CrossPointSettings::READER_STATUS_BAR_CHAPTER_CLOCK;check(SETTINGS.migrate({uint8_t(4),std::nullopt,std::nullopt})&&SETTINGS.statusBarItemsMode==4&&SETTINGS.statusBarChapterPageCount==0&&SETTINGS.statusBarBookProgressPercentage==0,"Missing itemsMode migrates current preset");SETTINGS.statusBarTitle=CrossPointSettings::HIDE_TITLE;check(!SETTINGS.migrate({uint8_t(4),std::nullopt,uint8_t(4)})&&SETTINGS.statusBarTitle==CrossPointSettings::HIDE_TITLE,"New matching marker preserves custom values");reset();check(SETTINGS.migrate({std::nullopt,uint8_t(1),std::nullopt})&&SETTINGS.readerStatusBarMode==0,"Legacy hidden reader migrates OFF");reset();check(SETTINGS.migrate({std::nullopt,uint8_t(0),std::nullopt})&&SETTINGS.readerStatusBarMode==2,"Legacy visible reader migrates DEFAULT");}
 check(lockDepth==0,"All render locks released");printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
