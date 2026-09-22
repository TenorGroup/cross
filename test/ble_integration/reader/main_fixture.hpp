struct GPIO{bool pressed=false,released=false,touch=false;bool wasAnyPressed()const{return pressed;}bool wasAnyReleased()const{return released;}bool wasTouchActivity()const{return touch;}}gpio;
struct Tilt{bool active=false;bool hadActivity(){const bool value=active;active=false;return value;}}halTiltSensor;
struct Power{int restores=0;void setPowerSaving(bool save){if(!save)restores++;}}powerManager;
int renderer=0;bool autoSleepBlockedUntilInput=false;
namespace filetransfer {bool active=false;bool isActive(){return active;}}
namespace freeink{
struct KeyEvent{int keycode=0,mods=0;};
struct BleKeyboardHost {bool running=true,stopping=false,connected=false,armResult=true;std::vector<KeyEvent>queue;std::vector<std::string>events;std::string armedAddr;int polls=0,armCalls=0;
 static BleKeyboardHost&getInstance(){static BleKeyboardHost h;return h;}
 bool isStopping()const{return stopping;}bool isRunning()const{return running;}bool isConnected()const{return connected;}
 void end(int){running=false;stopping=false;}bool armSelectedPeerReconnect(const char*addr){armCalls++;armedAddr=addr?addr:"";events.emplace_back("arm");return armResult;}void poll(){polls++;events.emplace_back("poll");}bool popKey(KeyEvent&e){if(queue.empty())return false;e=queue.front();queue.erase(queue.begin());return true;}};
namespace ble {bool stopped=false,init=false,startSuccess=true,rearm=false;int starts=0;
 void requestRearm(){rearm=true;}bool takeRearmRequest(){bool r=rearm;rearm=false;return r;}
 bool idleStopped(){return stopped;}void setIdleStopped(bool s){stopped=s;}bool initializing(){return init;}
 void setReaderStartDeferred(bool){}bool beginAsync(int&){starts++;BleKeyboardHost::getInstance().running=startSuccess;return startSuccess;}
 void stopForIdle(){stopped=true;BleKeyboardHost::getInstance().end(0);}void suspendForTransition(){BleKeyboardHost::getInstance().end(0);}}
}
