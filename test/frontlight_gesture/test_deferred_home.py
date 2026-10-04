"""Run the unchanged production Home-event, keyboard-save and deferred Pop bodies with RAM spies."""
from pathlib import Path
import subprocess
import sys
import tempfile

repo=Path(__file__).resolve().parents[2]
path=Path(sys.argv[1]) if len(sys.argv)>1 else repo/'src/activities/ActivityManager.cpp'
manager=path.read_text()
header=path.with_suffix('.h').read_text()
keyboard=(repo/'src/activities/util/KeyboardEntryActivity.cpp').read_text()

def block(source,start):
    brace=source.index('{',start); depth=0
    for i in range(brace,len(source)):
        if source[i]=='{': depth+=1
        elif source[i]=='}':
            depth-=1
            if not depth:return source[start:i+1]
    raise ValueError('Missing production block end')

begin=manager.index('    const bool heldBack =')
end=manager.index('    // Touch: the Home key',begin)
event=manager[begin:end]
pop=block(manager,manager.index('        if (homeAfterInput)'))
empty_pop=block(manager,manager.index('      if (stackActivities.empty())'))
restart=block(manager,manager.index('void ActivityManager::closeForRestart()'))
replace_begin=manager.index('      if (pendingAction == PendingAction::Replace)')
replace_end=manager.index('        // Destroy the current activity',replace_begin)
replace_reset=manager[replace_begin:replace_end]+'}\n'
fields=header[header.index('  bool homeAfterInput ='):header.index('  bool toReaderPage =')]
# Keep the old snapshot buildable: this fixture-only field receives no writes from legacy production bodies.
if 'homeAfterInputTarget' not in fields: fields+='  HomeMenuItem homeAfterInputTarget=HomeMenuItem::NONE;\n'
save=block(keyboard,keyboard.index('bool KeyboardEntryActivity::saveInputBeforeHome()'))
complete=block(keyboard,keyboard.index('void KeyboardEntryActivity::onComplete('))
fixture=r'''
#include <cassert>
#include <string>
#include <utility>
#include <vector>
#define LOG_DBG(...) ((void)0)
enum class HomeMenuItem {NONE,FILE_BROWSER,RECENTS,FAVORITES_TAB};
struct RenderLock {template<class T> explicit RenderLock(T&){} RenderLock(){} void unlock(){}};
struct KeyboardResult {std::string text;};
struct Activity {virtual ~Activity()=default; bool isHomeActivity()const{return false;}
  virtual bool saveInputBeforeHome(){return false;} bool handleHomeGesture(){return false;}
};
struct KeyboardEntryActivity:Activity {
  std::string text="Draft preserved";bool backCancels=false,popPending=false;std::string saved;
  bool saveInputBeforeHome() override;
  void onComplete(std::string text);
  void onCancel(){popPending=true;}
  void setResult(KeyboardResult result){saved=std::move(result.text);}
  void finish(){popPending=true;}
};
struct MappedInputManager {enum class Button{Back};bool bottom=true,heldBack=false,home=true;
  bool wasLongPressed(Button,int)const{return heldBack;}
  bool wasBottomHomeGesture()const{return bottom;}
  bool wasHomeGesture()const{return home;}
};
struct ActivityManager {
  MappedInputManager mappedInput;Activity* currentActivity=nullptr;
  HomeMenuItem preference=HomeMenuItem::FILE_BROWSER,reached=HomeMenuItem::NONE;int homeCalls=0;
  HomeMenuItem homeKeyTarget()const{return preference;}
  void goHome(HomeMenuItem target=HomeMenuItem::NONE){reached=target;++homeCalls;}
  enum class PendingAction{Replace};PendingAction pendingAction=PendingAction::Replace;
  std::vector<int> stackActivities;
  void exitActivity(RenderLock&){} void flushDeferredWrites(){}
  void receive();void completePop();void emptyPop();void replaceComplete();void closeForRestart();
'''
test=r'''
int main(){
  for(bool bottom:{true,false}){
    KeyboardEntryActivity keyboard;
    ActivityManager manager;manager.currentActivity=&keyboard;manager.mappedInput.bottom=bottom;
    manager.receive();
    assert(keyboard.popPending && keyboard.saved=="Draft preserved" && manager.homeCalls==0);
    manager.preference=HomeMenuItem::FAVORITES_TAB;
    manager.completePop();
    assert(manager.homeCalls==1);
    assert(manager.reached==(bottom?HomeMenuItem::RECENTS:HomeMenuItem::FILE_BROWSER));
    assert(!manager.homeAfterInput);
    manager.completePop();assert(manager.homeCalls==1);
    assert(manager.homeAfterInputTarget==HomeMenuItem::NONE);
    manager.homeAfterInput=true;manager.homeAfterInputTarget=HomeMenuItem::RECENTS;
    manager.emptyPop();assert(manager.reached==HomeMenuItem::RECENTS && !manager.homeAfterInput);
    assert(manager.homeAfterInputTarget==HomeMenuItem::NONE);
    manager.homeAfterInput=true;manager.homeAfterInputTarget=HomeMenuItem::RECENTS;
    manager.replaceComplete();assert(!manager.homeAfterInput && manager.homeAfterInputTarget==HomeMenuItem::NONE);
    manager.homeAfterInput=true;manager.homeAfterInputTarget=HomeMenuItem::RECENTS;
    manager.closeForRestart();assert(!manager.homeAfterInput && manager.homeAfterInputTarget==HomeMenuItem::NONE);
  }
}
'''
cpp=fixture+fields+'};\n'+save+'\n'+complete+'\n'
cpp+='void ActivityManager::receive(){\n'+event+'\n}\n'
cpp+='void ActivityManager::completePop(){RenderLock lock;for(int once=0;once<1;++once){\n'+pop+'\n}}\n'
cpp+='void ActivityManager::emptyPop(){RenderLock lock;for(int once=0;once<1;++once){\n'+empty_pop+'\n}}\n'
cpp+='void ActivityManager::replaceComplete(){\n'+replace_reset+'\n}\n'+restart+'\n'+test
with tempfile.TemporaryDirectory(prefix='deferred-home-production-') as tmp:
    source=Path(tmp)/'test.cpp';binary=Path(tmp)/'test'
    source.write_text(cpp)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('GREEN: production keyboard save -> deferred Pop keeps bottom Recent / physical chosen target, one Home')
