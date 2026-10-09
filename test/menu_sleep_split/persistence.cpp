#include <cstdio>
#include "MenuCustomization.cpp"
static int checks=0;
static bool check(bool value,const char* text) { ++checks; if(!value)std::fprintf(stderr,"FAIL %s\n",text); return value; }
static void reset() { menucustom::data=menucustom::State{}; menucustom::loaded=false; }
int main() {
  bool ok=true;
  reset(); menucustom::load();
  const std::array<uint8_t,9> defaults={0,7,1,2,8,3,4,5,6};
  ok &= check(menucustom::state().order[1]==defaults,"fresh default order");
  const char* path="/.crosspoint/menu-customization.json";
  Storage.files[path]=R"({"version":1,"tabs":{"settings":[6,3,0,5,1,4,2]},"pins":["settings/sleepScreen","settings/wakeButtons","settings/frontlightRestoreOnWake","settings/sleepTimeoutMinutes"]})";
  reset(); menucustom::load();
  const std::array<uint8_t,9> legacy={6,3,0,5,1,4,2,7,8};
  ok &= check(menucustom::state().order[1]==legacy,"legacy relative order preserved and new IDs appended");
  ok &= check(menucustom::save(),"save normalized legacy state");
  JsonDocument saved; deserializeJson(saved,Storage.files[path]);
  ok &= check(saved["version"].as<int>()==1 && saved["tabs"]["settings"].size()==9,"v1 persistence saves nine IDs");
  reset(); menucustom::load();
  ok &= check(menucustom::state().order[1]==legacy,"reload preserves migrated order");
  ok &= check(menucustom::state().pinCount==4 && menucustom::state().find("settings/wakeButtons")==1,"old pins survive save and reload");
  ok &= check(menucustom::adjacent(1,8,9,1)==6 && menucustom::adjacent(1,6,9,-1)==8,"navigation wraps custom order including Motion sensor");
  ok &= check(menucustom::moveTab(1,7,9,-1),"new tab can be reordered and saved");
  reset(); menucustom::load();
  ok &= check(menucustom::state().order[1][6]==7,"new tab reordered position survives restart");
  // A board without a motion sensor shows eight tabs: the stored ID 8 is stepped over.
  menucustom::state().order[1]={0,7,1,2,8,3,4,5,6};
  ok &= check(menucustom::idAt(1,4,8)==3 && menucustom::position(1,6,8)==7 && menucustom::idAt(1,7,8)==6,"hidden tab is skipped, the rest keep their order");
  ok &= check(menucustom::adjacent(1,2,8,1)==3 && menucustom::adjacent(1,3,8,-1)==2,"stepping tabs passes over the hidden one");
  ok &= check(menucustom::moveTab(1,3,8,-1) && menucustom::state().order[1]==std::array<uint8_t,9>{0,7,1,3,8,2,4,5,6},"moving a tab past the hidden one keeps it stored");
  Storage.files[path]=R"({"version":1,"tabs":{"settings":[6,6,255,-1,"bad",2,2]},"pins":["settings/sleepScreen","settings/sleepScreen"]})";
  reset(); menucustom::load();
  const std::array<uint8_t,9> sanitized={6,2,0,1,3,4,5,7,8};
  ok &= check(menucustom::state().order[1]==sanitized && menucustom::state().pinCount==1,"invalid duplicate incomplete order normalized and pin deduplicated");
  menucustom::save(); const auto backup=Storage.files[path];
  Storage.files["/.crosspoint/menu-customization.bak"]=backup; Storage.files[path]="{broken";
  reset(); menucustom::load();
  ok &= check(menucustom::state().order[1]==sanitized,"backup recovery retains new tab");
  const auto previous=menucustom::state().order[1]; Storage.writes=false;
  ok &= check(!menucustom::moveTab(1,7,9,-1) && menucustom::state().order[1]==previous,"failed save rolls back tab move");
  Storage.writes=true;
  Storage.files.clear();
  reset(); menucustom::load();
  const std::array<uint8_t,9> textDefaults={4,0,1,2,3,5,6,7,8};
  ok &= check(menucustom::state().order[3]==textDefaults,"fresh text menu puts Favorites before stable legacy IDs");
  Storage.files[path]=R"({"version":1,"tabs":{"text":[3,1,0,2],"reader":[3,0,2,1]},"pins":["text/fontSize","text/dropCapMode"]})";
  reset(); menucustom::load();
  const std::array<uint8_t,9> textLegacy={3,1,0,2,4};
  const std::array<uint8_t,9> readerLegacy={3,0,2,1,4};
  ok &= check(menucustom::state().order[3]==textLegacy,"legacy text order keeps four IDs and appends Favorites once");
  ok &= check(menucustom::state().order[2]==readerLegacy,"legacy reader order keeps four IDs and appends Text once");
  ok &= check(menucustom::idAt(3,0,4)==3 && menucustom::idAt(3,3,4)==2,"four-tab boards skip the added Favorites ID");
  ok &= check(menucustom::save(),"save both migrated five-tab menus");
  deserializeJson(saved,Storage.files[path]);
  ok &= check(saved["tabs"]["text"].size()==5 && saved["tabs"]["reader"].size()==5,"save retains all five text and reader IDs");
  reset(); menucustom::load();
  ok &= check(menucustom::state().order[3]==textLegacy && menucustom::state().order[2]==readerLegacy,"reload keeps both migrated menus");
  ok &= check(menucustom::state().pinCount==2 && menucustom::state().find("text/fontSize")==0 && menucustom::state().find("text/dropCapMode")==1,"text pins survive migration and save");
  ok &= check(menucustom::moveTab(3,4,5,-1),"added Favorites tab can move and save");
  reset(); menucustom::load();
  ok &= check(menucustom::state().order[3][3]==4 && menucustom::state().order[3][4]==2,"moved Favorites and Layout survive reload");
  Storage.files[path]=R"({"version":1,"tabs":{"text":[4,3,2,1,0],"reader":[4,3,2,1,0]},"pins":[]})";
  reset(); menucustom::load();
  const std::array<uint8_t,9> fiveTabs={4,3,2,1,0};
  ok &= check(menucustom::state().order[3]==fiveTabs && menucustom::state().order[2]==fiveTabs,"current five-tab files keep their IDs without duplication");
  ok &= check(menucustom::save(),"save current five-tab order");
  reset(); menucustom::load();
  ok &= check(menucustom::state().order[3]==fiveTabs && menucustom::state().order[2]==fiveTabs,"current five-tab order round trips");
  std::printf("%d checks\n",checks); return ok?0:1;
}
