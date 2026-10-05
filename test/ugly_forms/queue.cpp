#define QUEUE_HARNESS
#include "component.cpp"
#include <array>
#include <atomic>
// MappedInputManager is an inactive hardware boundary. Events are enqueued
// explicitly to exercise the production queue/drain code while a frame is busy.
namespace shell {bool isUgly(){return true;}}
namespace ugly {enum class Quip {Pin,Unpin};std::string quip(Quip){return "pin";}}
struct InputBoundary {
 enum class Button {Left,Right,Up,Down,Confirm,Back};enum class SwipeDir {None,Left,Right,Up,Down};
 bool wasLongPressed(Button,int){return false;}bool wasReleased(Button){return false;}
 bool wasHomeGesture(){return false;}bool wasScreenLongPress(int&,int&){return false;}bool wasScreenTapped(int&,int&){return false;}
 SwipeDir wasSwipe(){return SwipeDir::None;}
};
int lockDepth=0;bool availableLock=true;
struct RenderLock {
 struct TryTake{};bool got=true;
 explicit RenderLock(TryTake){got=availableLock;if(got)++lockDepth;}
 template<class T>explicit RenderLock(T&){++lockDepth;}
 ~RenderLock(){if(got)--lockDepth;}
 bool acquired(){return got;}
};
#define LOG_ERR(...) ((void)0)
// Production signatures refer to MappedInputManager nested enums.
#define MappedInputManager InputBoundary
struct SettingsActivity {
 struct FormEvent {enum class Type {Key,Tap,Hold,Pin};Type type;Sheet::Key key=Sheet::Key::Confirm;int16_t x=0,y=0;uint32_t surface=0;};
 InputBoundary mappedInput;
 struct Popup {template<class T>bool handleInput(InputBoundary&,T){return false;}}optionPopup;
 Sheet form_;Catalog catalog;GfxRenderer renderer;
 std::array<FormEvent,16>formQueue_{};uint8_t formHead_=0,formCount_=0;uint32_t formSurface_=1;
 std::atomic<uint32_t>formVisibleSurface_{1};std::atomic<bool>formPaintReady_{true},formPinFailed_{false};
 struct Nav {int selected=0;}nav;
 int applies=0,refreshes=0,commits=0;bool appliedUnderLock=false;
 void requestUpdate(){++refreshes;}
 Nav& activeNav(){return nav;}
 std::string favoriteKey(int){return {};}
 bool rowIsPinned(int){return false;}bool toggleFavorite(int){return true;}
 void applyFormIntent(const Sheet::Intent& i){++applies;appliedUnderLock|=lockDepth!=0;if(i.kind==Sheet::IntentKind::Commit)++commits;if(i.repaint)requestUpdate();}
 void queueForm(FormEvent);bool handleCustomInput();
 void bind(){catalog.rows={{1,"Font",Sheet::Kind::Paper,0,30}};form_.bind(renderer,catalog.view(),true);paint();}
 void paint(){
#undef MappedInputManager
::MappedInputManager input;form_.paint(renderer,input);
#define MappedInputManager InputBoundary
formVisibleSurface_.store(formSurface_);formPaintReady_.store(true);}
};
#undef MappedInputManager
// The hardware boundary name is substituted only for the extracted source.
#define MappedInputManager InputBoundary
#include "QueueMethods.inc"
#undef MappedInputManager
int main(){
 SettingsActivity a;a.bind();
 a.queueForm({SettingsActivity::FormEvent::Type::Key,Sheet::Key::Confirm});
 a.queueForm({SettingsActivity::FormEvent::Type::Tap,Sheet::Key::Confirm,100,220});
 a.formPaintReady_.store(false);a.handleCustomInput();check(a.formCount_==2&&a.applies==0,"busy frame retains queued events");
 a.paint();availableLock=false;a.handleCustomInput();check(a.formCount_==2&&a.applies==0,"busy mutex retains queued events");
 availableLock=true;a.handleCustomInput();check(a.form_.paperOpen()&&a.formCount_==1&&!a.formPaintReady_.load(),"paper opening waits for completed paint");
 check(!a.appliedUnderLock&&a.refreshes==1,"intent applied outside lock with 1refresh");
 a.handleCustomInput();check(a.formCount_==1,"old tap retained while new paper paints");
 a.paint();a.handleCustomInput();check(a.formCount_==0&&a.commits==0&&a.applies==1,"tap from previous surface dropped");
 a.queueForm({SettingsActivity::FormEvent::Type::Key,Sheet::Key::NextQuestion});a.handleCustomInput();check(a.form_.candidate()==1&&a.commits==0,"queued key belongs to current paper");
 a.paint();a.queueForm({SettingsActivity::FormEvent::Type::Hold,Sheet::Key::Confirm,100,220});++a.formSurface_;a.handleCustomInput();check(a.formCount_==0&&a.applies==2,"hold from previous surface dropped");
 check(lockDepth==0&&!a.appliedUnderLock,"all queue paths release lock");
 printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
