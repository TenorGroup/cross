#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
// The order of the radio start and the reader's release before it.
inline std::vector<std::string> radioSteps;
#include <cstring>
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define CROSSPOINT_BLE_HID_HOST 1
#define NEW_PIPELINE @@NEW@@
#define LAYOUT_HOOK @@LAYOUT@@
uint32_t nowMs=1000;
uint32_t millis(){return nowMs;}
struct RenderLock { struct TryTake{}; static inline bool busy=false; bool owns=false;
 RenderLock(){ if(busy)throw std::runtime_error("recursive or blocking render lock");busy=owns=true; }
 template<class T> explicit RenderLock(T&):RenderLock(){}
 explicit RenderLock(TryTake){ if(!busy)busy=owns=true; }
 ~RenderLock(){if(owns)busy=false;}
 bool acquired()const{return owns;} static bool peek(){return busy;}
};
struct Activity {virtual ~Activity()=default;virtual bool isReaderActivity() const {return true;}};
struct ActivityManager {enum class PendingAction{None,Push};PendingAction pendingAction=PendingAction::None;
 std::shared_ptr<Activity>currentActivity;uint32_t generation=1;
 bool sleepTransition=false;bool exclusive=false,preventSleep=false;bool requiresExclusiveStorageLoop()const{return exclusive;}bool preventAutoSleep()const{return preventSleep;}bool isForegroundReaderReady()const;bool foregroundReaderHoldsRadio()const{return false;}bool radioReady=true;int radioReadyAsks=0;bool readyForegroundReaderForRadio(){radioReadyAsks++;radioSteps.emplace_back("ready");return radioReady;}void goToReader(const std::string&){}bool pageTurn(bool);bool chapterSkip(bool);uint32_t activityGeneration()const{return generation;}
 bool isForegroundReaderActivity()const{return pendingAction==PendingAction::None&&currentActivity&&currentActivity->isReaderActivity();}
};
ActivityManager activityManager;
struct FakeInput{enum class Button{PageBack,PageForward,Left,Right,Back,Confirm};bool prev=false,next=false,back=false,confirm=false;bool released=false;int releasedButton=-1;bool wasReleased(Button button)const{if(button==Button::Back)return back;if(button==Button::Confirm)return confirm;return released&&(releasedButton<0||releasedButton==static_cast<int>(button));} unsigned long getHeldTime()const{return 0;}};
using MappedInputManager=FakeInput;FakeInput mappedInputManager;
@@BLEBINDING@@
struct FakeSettings{blebinding::RemoteTable bleRemotes[blebinding::kMaxRemotes]={};uint8_t bleRemoteCount=0;enum Behavior{FONT_SIZE_STEP,CHAPTER_SKIP};Behavior longPressButtonBehavior=CHAPTER_SKIP;bool blePageTurnerEnabled=true;char blePeerAddr[18]={};enum class BlePageAction{PreviousPage,NextPage,None};BlePageAction blePageActionFor(int key,int mods)const{return mods?BlePageAction::None:(key==1?BlePageAction::NextPage:(key==2?BlePageAction::PreviousPage:BlePageAction::None));}}SETTINGS;
using CrossPointSettings=FakeSettings;
namespace ReaderUtils {constexpr int SKIP_HOLD_MS=500;
struct Turns{bool prev=false,next=false,prevLongPressed=false,nextLongPressed=false,fromTilt=false;};
struct Touch{bool prev=false,next=false;unsigned long heldMs=0;};
Turns detectPageTurn(FakeInput&i){Turns t;t.prev=i.prev;t.next=i.next;return t;}
Touch detectTouchPageTurn(int&,FakeInput&){return {};}bool isTouchMenuGesture(int&,FakeInput&){return false;}}
struct EndOfBookOptions{bool menu=false;bool menuActive()const{return menu;}};
struct FakeStats {uint32_t pages=0, records=0, habits=0;void record(uint32_t,uint32_t,uint16_t turns,int){pages+=turns;records++;}void observeHabits(uint32_t,uint16_t,uint32_t){habits++;}uint32_t currentDay(){return 1;}}READING_STATS;
struct ReaderActivity:Activity{
 bool preview=false;int renderer=0;FakeInput mappedInput;uint16_t trangDaLat=0;int requests=0,goHome=0;
 std::atomic<bool>pageReady{true};std::unique_ptr<EndOfBookOptions>endOfBookOptions=std::make_unique<EndOfBookOptions>();std::atomic<bool>endOfBookOptionsReady{false};
 int8_t pendingExternalTurn=0;uint32_t pendingExternalGeneration=0;bool pendingTurnIsLocal=false,pendingExternalChapter=false;int backCalls=0,formatCalls=0,chapterSkips=0;std::string bookPath="fixture.txt";void finish(){}
 bool statsEnabled=true,statsActive=false,statsDirty=false;uint32_t statsLastMs=0,statsSavedMs=0,statsDay=1,statsDayPollMs=0;
 virtual bool latTrangThat(bool)=0;virtual bool isAtEndOfBook()const=0;virtual void onReturnFromEndOfBook()=0;
 static constexpr int8_t MAX_QUEUED_TURNS=8;void queuePageTurn(bool,bool,const char*);
 bool pageTurn(bool);bool pageTurnLocked(bool);bool luotLatTrangNgoai(bool);bool luotNhayChuongNgoai(bool);bool processExternalPageTurn();void cancelExternalPageTurn();
 // Trinh doc khong co muc luc tra ve false: giu nut o do khong lam gi ca.
 virtual bool nhayChuongThat(int){return false;}
 virtual bool externalPageTurnAllowed()const;
 virtual bool manualPageTurnReady()const;
 virtual bool pageAwaitsLayout()const;
 bool isReaderActivity()const override{return !preview;}
 bool isPageReady()const{return pageReady.load();}
 void requestUpdate(){requests++;}
 void onGoHome(){goHome++;}
 bool handleEndOfBookPageTurn(bool,bool);bool endOfBookMenuActive()const;
 bool handlePreviewInput();void clearEndOfBookOptionsIfNeeded(){}
 // Lowers the exit flag after a dropped exit (ReaderActivity::stayAfterDroppedExit); no exit here.
 void stayAfterDroppedExit(){}
 bool handleEndOfBookMenu(){return endOfBookMenuActive();}bool handleFormatInput(){if(mappedInput.confirm){formatCalls++;return true;}return false;}bool handleBackNavigation(){if(mappedInput.back){backCalls++;return true;}return false;}
 bool docCoChuMotNac(int){return false;}bool skipPages(int a){return pageTurn(a>0);}
 void updateReadingTime(bool);struct Shot{int progressPercent=25;};Shot getScreenshotInfo()const{return {};}
 virtual void loop();
};
bool ActivityManager::isForegroundReaderReady()const{return isForegroundReaderActivity()&&static_cast<ReaderActivity*>(currentActivity.get())->isPageReady();}
#if !NEW_PIPELINE
bool ReaderActivity::externalPageTurnAllowed()const{return true;}
bool ReaderActivity::manualPageTurnReady()const{return true;}
#endif
#if !LAYOUT_HOOK
bool ReaderActivity::pageAwaitsLayout()const{return false;}
#endif
struct TxtReaderActivity:ReaderActivity {bool initialized=true;int currentPage=1,totalPages=4;
 bool latTrangThat(bool)override;bool isAtEndOfBook()const override;void onReturnFromEndOfBook()override;};
struct FakeXtc{unsigned getPageCount()const{return 4;}};
struct XtcReaderActivity:ReaderActivity{std::unique_ptr<FakeXtc>xtc=std::make_unique<FakeXtc>();unsigned currentPage=1;
 bool latTrangThat(bool)override;bool isAtEndOfBook()const override;void onReturnFromEndOfBook()override;};
struct Section{int currentPage=1,pageCount=4;bool building=false,partial=false;bool isBuilding()const{return building;}bool isPartial()const{return partial;}};
struct FakeEpub{int getSpineItemsCount()const{return 3;}};
struct EpubReaderActivity:ReaderActivity{
 enum class Overlay{None,Toolbar,WordPicker};Overlay overlay=Overlay::None;
 std::unique_ptr<Section>section=std::make_unique<Section>();std::unique_ptr<FakeEpub>epub=std::make_unique<FakeEpub>();
 bool nhayChuongThat(int huong)override{if(currentSpineIndex+huong<0)return false;chapterSkips++;currentSpineIndex+=huong;nextPageNumber=0;section.reset();return true;}
 std::atomic<bool>deferredClearPending{false};uint32_t lastPageTurnTime=0;int currentSpineIndex=0,nextPageNumber=0,pendingPageJump=0;
 int openedMenus=0,pendingManualTurn=0;void openOverlay(Overlay o){overlay=o;openedMenus++;}void openReaderMenu(){openedMenus++;}
 void loop()override;bool usesToolbarMenu()const{return true;}void handleOverlayInput(){}void discardOverlayPage(){}
 bool latTrangThat(bool)override;bool isAtEndOfBook()const override;void onReturnFromEndOfBook()override;
#if NEW_PIPELINE
 bool externalPageTurnAllowed()const override;bool manualPageTurnReady()const override;
#endif
#if LAYOUT_HOOK
 bool pageAwaitsLayout()const override;
#endif
};
