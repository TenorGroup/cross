struct GPIO{bool pressed=false,released=false,touch=false;bool wasAnyPressed()const{return pressed;}bool wasAnyReleased()const{return released;}bool wasTouchActivity()const{return touch;}}gpio;
struct Tilt{bool active=false;bool hadActivity(){const bool value=active;active=false;return value;}}halTiltSensor;
struct Power{int restores=0;void setPowerSaving(bool save){if(!save)restores++;}}powerManager;
int renderer=0;bool autoSleepBlockedUntilInput=false;
namespace filetransfer {bool active=false;bool isActive(){return active;}}
namespace freeink{
struct KeyEvent{int keycode=0,mods=0;bool pressed=true;};
struct RawButtonEvent{uint8_t reportId=0,byteIndex=0,value=0;bool pressed=true;uint8_t keycode=0,mods=0;uint32_t atMs=0;uint32_t code()const{return value|byteIndex<<8|reportId<<16;}};
struct BleKeyboardHost {bool running=true,stopping=false,connected=false,armResult=true,streamFresh=false;std::vector<KeyEvent>queue;std::vector<RawButtonEvent>raw;std::string addr,name;std::vector<std::string>events;std::string armedAddr;int polls=0,armCalls=0;
 static BleKeyboardHost&getInstance(){static BleKeyboardHost h;return h;}
 bool isStopping()const{return stopping;}bool isRunning()const{return running;}bool isConnected()const{return connected;}bool reportStreamFresh()const{return streamFresh;}
 void end(int){running=false;stopping=false;}bool armSelectedPeerReconnect(const char*addr){armCalls++;armedAddr=addr?addr:"";events.emplace_back("arm");return armResult;}void poll(){polls++;events.emplace_back("poll");}bool popKey(KeyEvent&e){if(queue.empty())return false;e=queue.front();queue.erase(queue.begin());return true;}
 bool popRawButton(RawButtonEvent&e){if(raw.empty())return false;e=raw.front();raw.erase(raw.begin());return true;}const char*connectedAddr()const{return connected?addr.c_str():"";}const char*connectedName()const{return connected?name.c_str():"";}};
namespace ble {bool stopped=false,init=false,startSuccess=true,rearm=false;int starts=0;
 bool held=false;bool radioHeldForBuild(){return held;}void setRadioHeldForBuild(bool h){held=h;}
 void requestRearm(){rearm=true;held=false;}bool takeRearmRequest(){bool r=rearm;rearm=false;return r;}
 bool idleStopped(){return stopped;}void setIdleStopped(bool s){stopped=s;}bool initializing(){return init;}
 bool deferred=false;void setReaderStartDeferred(bool d){deferred=d;}bool readerStartDeferred(){return deferred;}bool beginAsync(int&){starts++;BleKeyboardHost::getInstance().running=startSuccess;return startSuccess;}
 void stopForIdle(){stopped=true;BleKeyboardHost::getInstance().end(0);}void suspendForTransition(){BleKeyboardHost::getInstance().end(0);}}
}
// The shared quick action catalog's one door (main.cpp runQuickAction): recorded, so a case
// sees which catalog action a trigger asked for and from where.
namespace quickaction {enum class Trigger : uint8_t {PowerRelease,Shake,FaceDown,FaceUp,DoubleTap,Remote};}
std::vector<std::pair<uint8_t,quickaction::Trigger>> quickActions;
void runQuickAction(const uint8_t action,const quickaction::Trigger trigger){quickActions.emplace_back(action,trigger);}
