struct GPIO{bool pressed=false,released=false,touch=false;bool wasAnyPressed()const{return pressed;}bool wasAnyReleased()const{return released;}bool wasTouchActivity()const{return touch;}}gpio;
struct Tilt{bool active=false;bool hadActivity(){const bool value=active;active=false;return value;}}halTiltSensor;
struct Power{int restores=0;void setPowerSaving(bool save){if(!save)restores++;}}powerManager;
int renderer=0;bool autoSleepBlockedUntilInput=false;
constexpr int WIFI_MODE_NULL=0;struct FakeWiFi{int getMode()const{return WIFI_MODE_NULL;}}WiFi;
namespace filetransfer {bool active=false;bool isActive(){return active;}}
// The remote as the SDK hands it over: queues of key events and raw edges, link state.
namespace freeink{
struct KeyEvent{int keycode=0,mods=0;bool pressed=true;};
struct RawButtonEvent{uint8_t reportId=0,byteIndex=0,value=0;bool pressed=true;uint8_t keycode=0,mods=0;uint32_t atMs=0;uint32_t code()const{return value|byteIndex<<8|reportId<<16;}};
struct BleKeyboardHost {bool running=true,stopping=false,connected=false,armResult=true;std::vector<KeyEvent>queue;std::vector<RawButtonEvent>raw;std::string addr,name;std::vector<std::string>events;std::string armedAddr;int polls=0,armCalls=0;
 static BleKeyboardHost&getInstance(){static BleKeyboardHost h;return h;}};
// Knobs of the fake radio. A start runs on its own task on the device: here it is queued and
// the pump runs it around each pass, unless `init` holds it in flight.
namespace ble {bool init=false,startSuccess=true,startQueued=false;int starts=0;
 bool idleStopped(){return bleturner::status().idleStopped;}bool readerStartDeferred(){return bleturner::status().readerDeferred;}
 void requestRearm(){bleturner::afterPaint();}
 void runQueuedStart(){if(startQueued&&!init){startQueued=false;bleturner::detail::startTask();}}}
}
// The page turner's radio port over the fake remote (the device links SdkRadio.cpp here).
namespace bleturner::port {
using freeink::BleKeyboardHost;
Memo memo{0};
bool compiledIn(){return true;}
bool begin(){auto&h=BleKeyboardHost::getInstance();freeink::ble::starts++;radioSteps.emplace_back("start");h.running=freeink::ble::startSuccess;return freeink::ble::startSuccess;}
bool end(uint32_t){auto&h=BleKeyboardHost::getInstance();h.running=false;h.stopping=false;return true;}
bool running(){return BleKeyboardHost::getInstance().running;}
bool stopping(){return BleKeyboardHost::getInstance().stopping;}
bool connected(){return BleKeyboardHost::getInstance().connected;}
bool connecting(){return false;}
bool scanning(){return false;}
void poll(){auto&h=BleKeyboardHost::getInstance();h.polls++;h.events.emplace_back("poll");}
bool popRaw(RawEdge&out){auto&h=BleKeyboardHost::getInstance();if(h.raw.empty())return false;const auto e=h.raw.front();h.raw.erase(h.raw.begin());out.reportId=e.reportId;out.byteIndex=e.byteIndex;out.value=e.value;out.pressed=e.pressed;out.keycode=e.keycode;out.mods=e.mods;out.atMs=e.atMs;return true;}
bool popKey(KeyPress&out){auto&h=BleKeyboardHost::getInstance();if(h.queue.empty())return false;const auto e=h.queue.front();h.queue.erase(h.queue.begin());out.keycode=static_cast<uint8_t>(e.keycode);out.mods=static_cast<uint8_t>(e.mods);out.pressed=e.pressed;return true;}
bool armReconnect(const char*addr){auto&h=BleKeyboardHost::getInstance();h.armCalls++;h.armedAddr=addr?addr:"";h.events.emplace_back("arm");return h.armResult;}
Peer linked(){auto&h=BleKeyboardHost::getInstance();return h.connected?Peer{h.addr.c_str(),h.name.c_str()}:Peer{"",""};}
void scan(uint32_t){}bool connect(const char*){return false;}void disconnect(){}void forget(const char*){}
bool takeConnectFailure(char*,size_t){return false;}uint8_t bondCount(){return 0;}Peer bond(uint8_t){return {"",""};}uint8_t foundCount(){return 0;}Peer found(uint8_t){return {"",""};}
bool spawnStart(){if(freeink::ble::startQueued)throw std::runtime_error("two radio starts queued");freeink::ble::startQueued=true;return true;}
uint32_t nowMs(){return ::nowMs;}
void sleepMs(uint32_t ms){::nowMs+=ms;}
Memo&restartMemo(){return memo;}
}
// The shared quick action catalog's one door (main.cpp runQuickAction): recorded, so a case
// sees which catalog action a trigger asked for and from where.
namespace quickaction {enum class Trigger : uint8_t {PowerRelease,Shake,FaceDown,FaceUp,DoubleTap,Remote,EdgeTap,ScreenTap};}
std::vector<std::pair<uint8_t,quickaction::Trigger>> quickActions;
void runQuickAction(const uint8_t action,const quickaction::Trigger trigger){quickActions.emplace_back(action,trigger);}
