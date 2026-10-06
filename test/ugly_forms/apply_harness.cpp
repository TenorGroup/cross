#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
int lockDepth=0;bool saveUnderLock=false;
struct CrossPointSettings {
 uint8_t uiTextSize=0,uiUglyLevel=0,uiShell=0,sleepScreen=0,quickResumeSleepScreen=0,value=0;
 bool saveOk=true;int saves=0;
 bool saveToFile(){saveUnderLock|=lockDepth!=0;++saves;return saveOk;}
} SETTINGS;
namespace shell {enum class Kind:uint8_t {Cross=0,Ugly=1};bool isUgly(){return SETTINGS.uiShell==1;}int changes=0;void changed(){++changes;}}
namespace ugly {template<class I>int makeSwitchConfirm(int,I&,bool=false){return 0;}}
struct ActivityResult {bool isCancelled=false;};
enum class SettingType {TOGGLE,ENUM,ACTION,VALUE,STRING};
struct SettingInfo {
 SettingType type=SettingType::ENUM;
 uint8_t CrossPointSettings::*valuePtr=&CrossPointSettings::value;
 std::function<uint8_t()> valueGetter;
 std::function<void(uint8_t)> valueSetter;
 std::vector<int> values{0,1,2};std::vector<std::string> enumStringValues;
 std::span<const int> enumLabels()const{return values;}
 struct Range {uint8_t min=0,max=20,step=2;} valueRange;
 int nameId=42;
};
int syncCalls=0;bool syncSleep=false,syncQuick=false;
void syncQuickResumeTimeoutForSleepScreen(bool a,bool b){++syncCalls;syncSleep=a;syncQuick=b;}
struct MappedInputManager {enum class Button {Confirm,Back};bool back=true;bool wasReleased(Button b){return b==Button::Back&&back;}};
struct RenderLock {template<class T>explicit RenderLock(T&){++lockDepth;}~RenderLock(){--lockDepth;} };
struct SettingsActivity {
 std::vector<SettingInfo> rows{SettingInfo{}};
 std::vector<SettingInfo>*currentSettings=&rows;int settingsCount=1;
 int renderer=0,refreshes=0,rebuilds=0,notes=0,fontCalls=0;
 MappedInputManager mappedInput;bool fromHomeGroup=true;int exits=0;void finish(){++exits;}void onGoHome(){++exits;}
 struct FormRow {int selected;};
 static FormRow formRow(void* context,int){auto& a=*static_cast<SettingsActivity*>(context);return {SETTINGS.*a.rows[0].valuePtr};}
 void prepareFormQuip(int,int){}
 bool handleBack();
 bool fontUnderLock=false,fontOk=true;std::atomic<bool> saveFailed{false};
 std::function<void(const ActivityResult&)> confirmation;
 void requestUpdate(){++refreshes;}
 bool applyUiSettingChange(uint8_t CrossPointSettings::*,uint8_t){fontUnderLock|=lockDepth!=0;++fontCalls;return fontOk;}
 void noteValue(int){++notes;}
 void rebuildSettingsLists(){++rebuilds;}void bindForm(){++rebuilds;}
 template<class T>void startActivityForResult(int,T callback){confirmation=callback;}
 bool saveSettings(bool=true);bool applySettingValue(int,uint8_t,bool=false);
};
#include "ApplyMethods.inc"
int checks=0,failures=0;
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::cout<<"FAIL "<<name<<'\n';}}
void reset(){SETTINGS={};saveUnderLock=false;shell::changes=0;syncCalls=0;syncSleep=false;syncQuick=false;}
int main(){
 {reset();SettingsActivity a;check(a.applySettingValue(0,1),"valid choice applied");check(SETTINGS.value==1&&SETTINGS.saves==1,"1commit1save");check(a.notes==1&&a.rebuilds==1&&a.refreshes==1,"1commit1rebuild1refresh");check(a.applySettingValue(0,1)&&SETTINGS.saves==1,"committed choice harmless");check(!a.applySettingValue(0,9)&&SETTINGS.saves==1,"invalid choice rejected");check(!a.applySettingValue(-1,1)&&!a.applySettingValue(1,1),"row bounds");}
 {reset();SettingsActivity a;a.rows[0].type=SettingType::TOGGLE;check(!a.applySettingValue(0,2),"toggle range");check(a.applySettingValue(0,1)&&SETTINGS.value==1&&SETTINGS.saves==1,"toggle commits once");}
 {reset();SettingsActivity a;a.rows[0].type=SettingType::VALUE;a.rows[0].valueRange={4,18,2};check(!a.applySettingValue(0,3)&&!a.applySettingValue(0,5)&&!a.applySettingValue(0,20),"ruler actual range/step");check(a.applySettingValue(0,18)&&SETTINGS.value==18&&SETTINGS.saves==1,"ruler actual value");}
 {reset();SettingsActivity a;int dynamic=0,sets=0;a.rows[0].valuePtr=nullptr;a.rows[0].valueGetter=[&]{return dynamic;};a.rows[0].valueSetter=[&](uint8_t n){dynamic=n;++sets;};a.rows[0].enumStringValues={"A","B","C","D"};check(a.applySettingValue(0,3)&&dynamic==3&&sets==1&&SETTINGS.saves==1,"dynamic options/setter");check(a.applySettingValue(0,3)&&sets==1&&SETTINGS.saves==1,"dynamic selected no-op");}
 {reset();SettingsActivity a;a.rows[0].valuePtr=&CrossPointSettings::uiTextSize;a.fontOk=false;check(!a.applySettingValue(0,1)&&SETTINGS.uiTextSize==0&&SETTINGS.saves==0,"UIFont failure preserves value/save");check(a.fontCalls==1&&a.refreshes==1,"UIFont failure refresh once");}
 {reset();SettingsActivity a;a.rows[0].valuePtr=&CrossPointSettings::uiShell;check(!a.applySettingValue(0,1)&&SETTINGS.uiShell==0&&SETTINGS.saves==0&&bool(a.confirmation),"shell candidate requires confirmation");a.confirmation({true});check(SETTINGS.uiShell==0&&SETTINGS.saves==0&&shell::changes==0,"shell cancel preserves");a.confirmation({false});check(SETTINGS.uiShell==1&&SETTINGS.saves==1&&shell::changes==1,"shell confirm commits once");}
 {reset();SettingsActivity a;a.rows[0].valuePtr=&CrossPointSettings::sleepScreen;check(a.applySettingValue(0,1)&&syncCalls==1&&syncSleep&&!syncQuick,"sleep pairing called");}
 {reset();SettingsActivity a;SETTINGS.saveOk=false;a.applySettingValue(0,1);check(SETTINGS.saves==1&&a.saveFailed.load()&&a.refreshes==1,"save failure propagated to visible flag");}
 {reset();SettingsActivity a;SETTINGS.saveOk=false;a.applySettingValue(0,1);check(a.handleBack()&&a.exits==0&&a.saveFailed.load()&&SETTINGS.value==1,"Back failure blocks exit and preserves RAM value");SETTINGS.saveOk=true;check(a.handleBack()&&a.exits==1&&!a.saveFailed.load()&&SETTINGS.saves==3,"Back retry saves then returns parent");}
 {reset();SettingsActivity a;SETTINGS.uiShell=1;check(a.applySettingValue(0,1)&&SETTINGS.value==1&&SETTINGS.saves==1,"ugly shared commit applied");check(a.refreshes==0&&a.rebuilds==0,"ugly commit delegates 1refresh to outer intent");check(!saveUnderLock&&!a.fontUnderLock&&lockDepth==0,"save/font outside render lock");}
 std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
