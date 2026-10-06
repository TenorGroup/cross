#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <BoardConfig.h>
#include <HalClock.h>
#include <HalTiltSensor.h>
#include <SdCardFontRegistry.h>
#include "SettingsList.h"
#include "MenuCustomization.h"
#include "activities/util/KeyboardLayoutSet.h"
#include "Boundaries.inc"
// The settings constructor is defined in src/CrossPointSettings.cpp, outside this slice;
// it only lays the tenor/cross setup over the member initializers, as this one does.
CrossPointSettings::CrossPointSettings() { applyTenorPreset(); }
namespace menucustom {
State& state() { static State data; return data; }
bool save() { return true; }
}
struct SettingsActivity {
  // Same shape as freeink::ui::ListNav: the selection is atomic (read by the render task).
  struct Cursor { std::atomic<int> selected{0}; bool followOnBuild=false; };
  struct Input { bool hasTouch() const { return BoardConfig::hasTouch(); } } mappedInput;
  int selectedCategoryIndex=0, settingsCount=0;
  static constexpr int categoryCount=settingstabs::TAB_COUNT;
#include "Fields.inc"
  const std::vector<SettingInfo>* currentSettings=nullptr;
  std::array<Cursor, settingstabs::TAB_COUNT> tabNavs{};
  void rebuildRowItems() {}
  Cursor& activeNav() { return tabNavs[selectedCategoryIndex]; }
  int ringPos() const { return tabNavs[selectedCategoryIndex].selected; }
  void selectCategory(int tab) {
    selectedCategoryIndex=tab;
    currentSettings=&danhSachCuaThe(static_cast<settingstabs::Tab>(tab));
    settingsCount=currentSettings->size();
  }
  std::vector<SettingInfo>& danhSachCuaThe(settingstabs::Tab);
  static bool listedAsRow(const SettingInfo& setting);
  struct FormBoundary { void invalidate() {} } form_;
  std::atomic<bool> formPaintReady_{false};
  void bindForm() {}
  void focusForm(int) {}
  void rebuildSettingsLists(bool lockHeld = false);
  std::string favoriteKey(int) const;
  int focusFavorite(const std::string&);
};
// The X4 Pro About & updates screen (InfoUpdateActivity) holds no rows on these boards.
namespace infoupdate { inline bool holds(settingstabs::Action, bool = false) { return false; } }
#include "Methods.inc"
#include "HomeSettings.inc"
struct HomeSettings {
  std::vector<int> settingsGroups;
  std::vector<std::string> rowLabels;
  void build() {
    rowLabels.emplace_back(tr(STR_FILE_TRANSFER));
    auto groups = homerows::settingsGroups();
    settingsGroups = std::move(groups.ids);
    for (auto& label : groups.labels) rowLabels.push_back(std::move(label));
  }
};
// Preserve symbolic label identities across unrelated translation regeneration.
static const char* names[] = {
#include "KeyNames.inc"
};
const char* trWeb(Language, StrId id) { return names[static_cast<int>(id)]; }
const char* plainButtonText(const char* text) { return text; }
void yield() {}
void resetTaskWatchdogIfSubscribed() {}
constexpr int CONTENT_LENGTH_UNKNOWN=-1;
struct WebBoundary {
  std::string output;
  void setContentLength(int) {}
  void send(int, const char*, const char*) {}
  void sendContent(const char* s) { output += s; }
};
struct CrossPointWebServer {
  std::unique_ptr<WebBoundary> server=std::make_unique<WebBoundary>();
  Language requestLanguage() const { return Language::EN; }
  void handleGetSettings() const;
};
#include "StatusItems.inc"
#include "Web.inc"
static int checks=0;
static bool check(bool value, const char* message) {
  ++checks;
  if (!value) std::fprintf(stderr,"FAIL %s\n",message);
  return value;
}
int main(int argc, char** argv) {
  const std::string board=argc>1?argv[1]:"x3";
  const bool imu=argc>2?std::atoi(argv[2]):true;
  const bool optional=argc>3?std::atoi(argv[3]):false;
  BoardConfig::ACTIVE=board=="x3"?BoardConfig::XTEINK_X3:board=="x4"?BoardConfig::XTEINK_X4:BoardConfig::XTEINK_X4_PRO;
  gpio.x3=board=="x3";
  halTiltSensor.available=imu;
  halClock.available=optional;
  SETTINGS.shortPwrBtn=optional?CrossPointSettings::FOOTNOTES:CrossPointSettings::IGNORE;
  if(optional) discovered.push_back({"Dictionary", "dict"});
  CrossPointWebServer web;
  web.handleGetSettings();
  bool ok=true;
  if (BoardConfig::isX4Pro())
    ok &= check(SETTINGS.homeButtonDoubleTapAction == static_cast<uint8_t>(HomeButtonAction::Ignore),
                "touch Home double tap defaults to Ignore so single tap has no double tap wait");
  ok &= check(settingstabs::TAB_COUNT==9,"nine settings tabs");
  ok &= check(static_cast<int>(settingstabs::Tab::OTHER)==6,"old tab IDs preserved");
  SettingsActivity activity;
  activity.rebuildSettingsLists();
  const auto& catalog=getBaseSettingsList();
  std::map<std::string,int> occurrences, routes;
  JsonDocument result;
  auto counts=result["counts"].to<JsonArray>();
  for(int tab=0;tab<settingstabs::TAB_COUNT;++tab) {
    const auto& rows=activity.danhSachCuaThe(static_cast<settingstabs::Tab>(tab));
    counts.add(rows.size());
    for(const auto& row:rows) {
      const auto key=row.key?std::string("settings/")+row.key:"action/"+std::to_string(static_cast<int>(row.action));
      // A row without key or action (the read-only panel chip row) has no route to collide on.
      if(!row.key && row.action==SettingAction::None) continue;
      ++occurrences[key];
      // Tenor keeps the legacy action/2 pin on the battery and clock corners, so the
      // status bar row that shares its action is deliberately not pinnable there.
      if(row.action==SettingAction::CustomiseStatusBar) {
        activity.selectCategory(tab);
        ok &= check(activity.favoriteKey(static_cast<int>(&row-rows.data())).empty(),"Tenor status bar row is not pinnable");
        continue;
      }
      routes[key]=tab;
      const int index=activity.focusFavorite(key);
      ok &= check(index>=0 && activity.selectedCategoryIndex==tab,"every visible pin resolves to its tab");
      ok &= check(activity.favoriteKey(index)==key,"pin stable key round trip");
    }
  }
  for(const auto& [key,n]:occurrences) ok &= check(n==1,"each visible row has exactly one tab");
  const char* moved[]={"sleepScreen","sleepScreenCoverMode","sleepScreenCoverFilter","quickResumeSleepScreen","wakeIntoBook","sleepTimeoutMinutes"};
  for(const char* key:moved) ok &= check(routes[std::string("settings/")+key]==7,"moved sleep row resolves to tab ID7");
  ok &= check(!routes.count("settings/wakeButtons"),"retired wakeButtons row is gone on every board");
  const bool light=BoardConfig::hasPwmFrontlight()||BoardConfig::hasI2cFrontlight();
  ok &= check(light ? routes["settings/frontlightRestoreOnWake"]==7 : !routes.count("settings/frontlightRestoreOnWake"),"restore light belongs to Sleep only on supported board");
  const auto before=web.server->output;
  web.server->output.clear(); web.handleGetSettings();
  ok &= check(before==web.server->output,"grouping and pin route leave web JSON byte identical");
  HomeSettings home; home.build();
  // Motion sensor follows Controls, and only with the sensor.
  const std::vector<int> desired=imu?std::vector<int>{0,7,1,2,8,3,4,5,6}:std::vector<int>{0,7,1,2,3,4,5,6};
  ok &= check(home.settingsGroups==desired && home.rowLabels.size()==desired.size()+1,"Home exposes the default ordered groups plus file transfer");
  StrId labels[9]{};
  ok &= check(settingstabs::dongCuaTheCaiDat(labels,9)==9,"Home label helper includes nine groups");
  ok &= check(labels[8]==StrId::STR_CAT_OTHER,"default labels end with Other");
  ok &= check(imu ? routes["settings/tiltPageTurn"]==8 && routes["settings/faceUpAction"]==8 : !routes.count("settings/tiltPageTurn"),"sensor rows resolve to Motion sensor, ID8, only with the sensor");
  for(int i=0;i<settingstabs::TAB_COUNT;++i) {
    ok &= check(settingstabs::tenThe(static_cast<settingstabs::Tab>(i))!=StrId::STR_NONE_OPT,"every tab named");
    for(int j=0;j<i;++j) ok &= check(settingstabs::tenThe(static_cast<settingstabs::Tab>(i))!=settingstabs::tenThe(static_cast<settingstabs::Tab>(j)),"tab labels unique");
  }
  ok &= check(activity.focusFavorite("settings/wakeButtons")<0,"an old wakeButtons pin resolves to no row");
  result["catalog"]=catalog.size(); result["checks"]=checks;
  JsonDocument webDoc; deserializeJson(webDoc, before); result["web"]=webDoc;
  std::string output; serializeJson(result,output); std::puts(output.c_str());
  return ok?0:1;
}
