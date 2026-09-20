#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>
#include "SettingsList.h"
#include "MenuFavorites.h"
#include "MenuCustomization.h"
void runHomeFavorites(const SdCardFontRegistry&,std::vector<std::string>&,std::vector<std::string>&,std::vector<std::string>&);
namespace settings_test_io { extern int writes; void setNextRead(const JsonDocument&); }
namespace mem {
struct alignas(std::max_align_t) Header { size_t n; size_t epoch; };
size_t epoch=0,total=0,largest=0,live=0,peak=0,count=0; bool active=false;
void start(){++epoch;total=largest=live=peak=count=0;active=true;}
void report(const char* name,size_t size,size_t capacity){active=false;std::printf("%s size=%zu capacity=%zu sizeof_SettingInfo=%zu total=%zu largest=%zu live=%zu peak=%zu calls=%zu\n",name,size,capacity,sizeof(SettingInfo),total,largest,live,peak,count);}
}
void* operator new(size_t n){auto* h=static_cast<mem::Header*>(std::malloc(sizeof(mem::Header)+(n?n:1)));if(!h)throw std::bad_alloc();h->n=n;h->epoch=mem::active?mem::epoch:0;if(mem::active){mem::total+=n;mem::largest=std::max(mem::largest,n);mem::live+=n;mem::peak=std::max(mem::peak,mem::live);++mem::count;}return h+1;}
void operator delete(void* p) noexcept{if(!p)return;auto*h=static_cast<mem::Header*>(p)-1;if(h->epoch&&h->epoch==mem::epoch)mem::live-=h->n;std::free(h);}
void operator delete(void*p,size_t)noexcept{operator delete(p);}
void*operator new[](size_t n){return operator new(n);}void operator delete[](void*p)noexcept{operator delete(p);}void operator delete[](void*p,size_t)noexcept{operator delete(p);}
int main(int argc,char**argv){
 if(argc!=3)return 2;const std::string mode=argv[1];halTiltSensor.available=std::atoi(argv[2])!=0;
 if(mode=="cold") {mem::start();const auto&base=getBaseSettingsList();mem::report("cold",base.size(),base.capacity());bool ok=base.size()==base.capacity(); size_t tiltRows=0; for(const auto&row:base) if(row.nameId==StrId::STR_TILT_PAGE_TURN)++tiltRows; ok=ok && tiltRows==static_cast<size_t>(halTiltSensor.available);std::printf("capacity_exact=%s\n",ok?"GREEN":"RED");return ok?0:1;}
 const auto&base=getBaseSettingsList();(void)I18N.get(StrId::STR_FONT_FAMILY);
 SdCardFontRegistry registry;auto&families=const_cast<std::vector<SdCardFontFamilyInfo>&>(registry.getFamilies());
 families.push_back({"SD font family name number 1",{"font"},{{10,0,0},{14,0,0}},true});
 families.push_back({"SD font family name number 2",{"font"},{{12,0,0},{16,0,0}},true});
 std::vector<DictionaryEntry> dictionaries; dictionaries.push_back({"Long dictionary folder name for allocation one"}); dictionaries.push_back({"Long dictionary folder name for allocation two"});
 if(mode=="home" || mode=="home-file") {
  auto&pins=menucustom::state();pins.pinCount=mode=="home"?3:1;
  strcpy(pins.pins[0].data(),mode=="home"?"settings/uiTheme":"bookid/test");
  strcpy(pins.pins[1].data(),"text/fontFamily");strcpy(pins.pins[2].data(),"text/fontSize");
  strcpy(SETTINGS.sdFontFamilyName,families[0].name.c_str());
  std::vector<std::string> keys,values,labels;
  mem::start();runHomeFavorites(registry,keys,values,labels);mem::report(mode.c_str(),labels.size(),labels.capacity());
  if(keys.size()!=pins.pinCount || labels.size()!=pins.pinCount || values.size()!=pins.pinCount)return 9;
  if(mode=="home" && values[1]!=families[0].name)return 10;
  if(mode=="home-file" && labels[0]!="probe.epub")return 11;
  bool ok=mem::largest<4096;
  std::printf("home_consumer_no_full_catalog=%s\n",ok?"GREEN":"RED");return ok?0:1;
 }
 if(mode=="dynamic") {
  strcpy(SETTINGS.sdFontFamilyName,families[0].name.c_str());
  std::string fontValue;std::string sizeValue;
  mem::start();{fontValue=menufavorites::value("text/fontFamily",&registry);sizeValue=menufavorites::value("text/fontSize",&registry);mem::report("two_dynamic_favorites",2,0);}
  if(fontValue!=families[0].name || sizeValue.empty() || mem::largest>=4096)return 6;
  std::printf("dynamic_values_after_catalog_destroyed=GREEN font=%s size=%s\n",fontValue.c_str(),sizeValue.c_str());return 0;
 }
 if(mode=="favorites") {
  mem::start();{auto id=menufavorites::label("settings/uiTheme");if(id!=StrId::STR_UI_THEME)return 3;auto value=menufavorites::value("settings/uiTheme",&registry);if(value.empty())return 7;mem::report("one_favorite_direct",1,0);}
  std::printf("released_live=%zu\n",mem::live);bool ok=mem::largest<base.size()*sizeof(SettingInfo);std::printf("no_full_catalog_allocation=%s\n",ok?"GREEN":"RED");return ok?0:1;
 }
 if(mode=="category"){
  mem::start();std::vector<SettingInfo> rows;
  {auto catalog=getSettingsList(&registry,&dictionaries);for(const auto&row:catalog)if(row.category==StrId::STR_CAT_READER)rows.push_back(row);mem::report("reader_category_with_source_alive",rows.size(),rows.capacity());}
  std::printf("after_source_destroyed_live=%zu\n",mem::live);
  for(auto&row:rows)if(row.nameId==StrId::STR_FONT_FAMILY){row.valueSetter(2);if(row.enumStringValues.at(2)!=families[0].name||row.valueGetter()!=2)return 4;}
  for(auto&row:rows)if(row.nameId==StrId::STR_DICTIONARY){row.valueSetter(1);if(row.enumStringValues.at(1)!=dictionaries[0].name||row.valueGetter()!=1)return 5;}
  std::puts("dynamic_labels_after_source_destroyed=GREEN");return 0;
 }
 if(mode=="json") {JsonDocument doc;SETTINGS.toJson(doc);mem::start();SETTINGS.toJson(doc);mem::report("save_warm",0,0);if(mem::total!=0)return 8;mem::start();bool ok=SETTINGS.fromJson(doc.as<JsonVariantConst>());mem::report("load_warm",0,0);return ok && mem::total==0?0:5;}
 if(mode=="v108") {
  auto expect=[](bool value,const char* label){if(!value)std::printf("FAIL %s\n",label);return value;};
  const auto uiSize=std::find_if(base.begin(),base.end(),[](const SettingInfo& row){return row.key && std::strcmp(row.key,"uiTextSize")==0;});
  const auto inkWeight=std::find_if(base.begin(),base.end(),[](const SettingInfo& row){return row.key && std::strcmp(row.key,"readerInkWeight")==0;});
  bool ok=expect(uiSize!=base.end() && uiSize->type==SettingType::ENUM && uiSize->enumValues.size()==3,"ui size catalog");
  ok=expect(inkWeight!=base.end() && inkWeight->type==SettingType::ENUM && inkWeight->enumValues.size()==4,"ink catalog")&&ok;
  JsonDocument saved;SETTINGS.toJson(saved);ok=expect(saved["uiTextSize"].as<uint8_t>()==0 && saved["readerInkWeightVersion"].as<uint8_t>()==1,"default stamps")&&ok;
  JsonDocument medium;medium["uiTextSize"]=1;ok=expect(SETTINGS.fromJson(medium.as<JsonVariantConst>()),"medium load")&&ok;
  saved.clear();SETTINGS.toJson(saved);ok=expect(saved["uiTextSize"].as<uint8_t>()==1,"medium round trip")&&ok;
  JsonDocument corrupt;corrupt["uiTextSize"]=99;ok=expect(SETTINGS.fromJson(corrupt.as<JsonVariantConst>()),"corrupt UI load")&&ok;
  saved.clear();SETTINGS.toJson(saved);ok=expect(saved["uiTextSize"].as<uint8_t>()==0,"corrupt UI clamps")&&ok;
  JsonDocument legacy;ok=expect(SETTINGS.fromJson(legacy.as<JsonVariantConst>()),"absent UI load")&&ok;
  saved.clear();SETTINGS.toJson(saved);ok=expect(saved["uiTextSize"].as<uint8_t>()==0,"absent UI defaults")&&ok;
  const int legacyInputs[]={0,1,2,3,99};const uint8_t legacyExpected[]={0,1,1,0,0};
  for(size_t i=0;i<std::size(legacyInputs);++i){JsonDocument old;SETTINGS.toJson(old);old.remove("readerInkWeightVersion");old["readerInkWeight"]=legacyInputs[i];ok=SETTINGS.fromJson(old.as<JsonVariantConst>())&&ok;ok=expect(SETTINGS.readerInkWeight==legacyExpected[i],"legacy ink mapping")&&ok;}
  JsonDocument missingInk;SETTINGS.toJson(missingInk);missingInk.remove("readerInkWeightVersion");missingInk.remove("readerInkWeight");ok=expect(SETTINGS.fromJson(missingInk.as<JsonVariantConst>()),"missing ink load")&&ok;ok=expect(SETTINGS.readerInkWeight==0,"missing ink defaults")&&ok;
  JsonDocument currentCorrupt;SETTINGS.toJson(currentCorrupt);currentCorrupt["readerInkWeight"]=99;ok=expect(SETTINGS.fromJson(currentCorrupt.as<JsonVariantConst>()),"current corrupt ink load")&&ok;ok=expect(SETTINGS.readerInkWeight==0,"current corrupt ink clamps")&&ok;
  JsonDocument resaveOnce;SETTINGS.toJson(resaveOnce);resaveOnce.remove("readerInkWeightVersion");resaveOnce["readerInkWeight"]=2;settings_test_io::setNextRead(resaveOnce);ok=SETTINGS.loadFromFile()&&ok;ok=expect(SETTINGS.readerInkWeight==1&&settings_test_io::writes==1,"legacy resaves once")&&ok;ok=SETTINGS.loadFromFile()&&ok;ok=expect(SETTINGS.readerInkWeight==1&&settings_test_io::writes==1,"current stays stable")&&ok;
  std::printf("v108_catalog_persistence=%s\n",ok?"GREEN":"RED");return ok?0:12;
 }
 return 2;
}
