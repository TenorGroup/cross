#include <cstdio>
#include "MenuCustomization.cpp"
static int checks=0;
static bool check(bool value,const char* text) { ++checks; if(!value)std::fprintf(stderr,"FAIL %s\n",text); return value; }
static void reset() { menucustom::data=menucustom::State{}; menucustom::loaded=false; }
int main() {
  bool ok=true;
  reset(); menucustom::load();
  const std::array<uint8_t,8> defaults={0,7,1,2,3,4,5,6};
  ok &= check(menucustom::state().order[1]==defaults,"fresh default order");
  const char* path="/.crosspoint/menu-customization.json";
  Storage.files[path]=R"({"version":1,"tabs":{"settings":[6,3,0,5,1,4,2]},"pins":["settings/sleepScreen","settings/wakeButtons","settings/frontlightRestoreOnWake","settings/sleepTimeoutMinutes"]})";
  reset(); menucustom::load();
  const std::array<uint8_t,8> legacy={6,3,0,5,1,4,2,7};
  ok &= check(menucustom::state().order[1]==legacy,"legacy relative order preserved and new ID appended");
  ok &= check(menucustom::save(),"save normalized legacy state");
  JsonDocument saved; deserializeJson(saved,Storage.files[path]);
  ok &= check(saved["version"].as<int>()==1 && saved["tabs"]["settings"].size()==8,"v1 persistence saves eight IDs");
  reset(); menucustom::load();
  ok &= check(menucustom::state().order[1]==legacy,"reload preserves migrated order");
  ok &= check(menucustom::state().pinCount==4 && menucustom::state().find("settings/wakeButtons")==1,"old pins survive save and reload");
  ok &= check(menucustom::adjacent(1,7,8,1)==6 && menucustom::adjacent(1,6,8,-1)==7,"navigation wraps custom order including Sleep");
  ok &= check(menucustom::moveTab(1,7,8,-1),"new tab can be reordered and saved");
  reset(); menucustom::load();
  ok &= check(menucustom::state().order[1][6]==7,"new tab reordered position survives restart");
  Storage.files[path]=R"({"version":1,"tabs":{"settings":[6,6,255,-1,"bad",2,2]},"pins":["settings/sleepScreen","settings/sleepScreen"]})";
  reset(); menucustom::load();
  const std::array<uint8_t,8> sanitized={6,2,0,1,3,4,5,7};
  ok &= check(menucustom::state().order[1]==sanitized && menucustom::state().pinCount==1,"invalid duplicate incomplete order normalized and pin deduplicated");
  menucustom::save(); const auto backup=Storage.files[path];
  Storage.files["/.crosspoint/menu-customization.bak"]=backup; Storage.files[path]="{broken";
  reset(); menucustom::load();
  ok &= check(menucustom::state().order[1]==sanitized,"backup recovery retains new tab");
  const auto previous=menucustom::state().order[1]; Storage.writes=false;
  ok &= check(!menucustom::moveTab(1,7,8,-1) && menucustom::state().order[1]==previous,"failed save rolls back tab move");
  std::printf("%d checks\n",checks); return ok?0:1;
}
