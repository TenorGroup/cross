struct GPIO{bool pressed=false,released=false,touch=false;bool wasAnyPressed()const{return pressed;}bool wasAnyReleased()const{return released;}bool wasTouchActivity()const{return touch;}}gpio;
struct Tilt{bool active=false;bool hadActivity()const{return active;}}halTiltSensor;
struct Power{int restores=0;void setPowerSaving(bool save){if(!save)restores++;}}powerManager;
int renderer=0;bool autoSleepBlockedUntilInput=false;
namespace filetransfer {bool active=false;bool isActive(){return active;}}
namespace freeink{
struct KeyEvent{int keycode=0,mods=0;};
struct BleKeyboardHost {bool running=true,stopping=false,connected=false;std::vector<KeyEvent>queue;int polls=0;
 static BleKeyboardHost&getInstance(){static BleKeyboardHost h;return h;}
 bool isStopping()const{return stopping;}bool isRunning()const{return running;}bool isConnected()const{return connected;}
 void end(int){running=false;stopping=false;}void poll(){polls++;}bool popKey(KeyEvent&e){if(queue.empty())return false;e=queue.front();queue.erase(queue.begin());return true;}};
namespace ble {bool stopped=false,init=false,startSuccess=true;int starts=0;
 bool idleStopped(){return stopped;}void setIdleStopped(bool s){stopped=s;}bool initializing(){return init;}
 void setReaderStartDeferred(bool){}bool beginAsync(int&){starts++;BleKeyboardHost::getInstance().running=startSuccess;return startSuccess;}
 void stopForIdle(){stopped=true;BleKeyboardHost::getInstance().end(0);}void suspendForTransition(){BleKeyboardHost::getInstance().end(0);}}
}
