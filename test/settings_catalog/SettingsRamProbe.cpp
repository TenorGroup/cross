#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <optional>
#include <string>
#include <vector>
#include "SettingsList.h"
#include "MenuFavorites.h"
#include "MenuCustomization.h"
#include "activities/reader/ReaderMenuLayout.h"
#include "ReaderTextRowSlice.h"
void runHomeFavorites(const SdCardFontRegistry&,std::vector<std::string>&,std::vector<std::string>&,std::vector<std::string>&);
namespace settings_test_io { extern int writes; void setNextRead(const JsonDocument&); }
namespace mem {
struct alignas(std::max_align_t) Header { size_t n; size_t epoch; };
size_t epoch=0,total=0,largest=0,live=0,peak=0,count=0; bool active=false,rejectAlloc=false;
void start(){++epoch;total=largest=live=peak=count=0;active=true;}
void report(const char* name,size_t size,size_t capacity){active=false;std::printf("%s size=%zu capacity=%zu sizeof_SettingInfo=%zu total=%zu largest=%zu live=%zu peak=%zu calls=%zu\n",name,size,capacity,sizeof(SettingInfo),total,largest,live,peak,count);}
}
void* operator new(size_t n){if(mem::active&&mem::rejectAlloc)throw std::bad_alloc();auto* h=static_cast<mem::Header*>(std::malloc(sizeof(mem::Header)+(n?n:1)));if(!h)throw std::bad_alloc();h->n=n;h->epoch=mem::active?mem::epoch:0;if(mem::active){mem::total+=n;mem::largest=std::max(mem::largest,n);mem::live+=n;mem::peak=std::max(mem::peak,mem::live);++mem::count;}return h+1;}
void operator delete(void* p) noexcept{if(!p)return;auto*h=static_cast<mem::Header*>(p)-1;if(h->epoch&&h->epoch==mem::epoch)mem::live-=h->n;std::free(h);}
void operator delete(void*p,size_t)noexcept{operator delete(p);}
void*operator new[](size_t n){return operator new(n);}void operator delete[](void*p)noexcept{operator delete(p);}void operator delete[](void*p,size_t)noexcept{operator delete(p);}
int main(int argc,char**argv){
 if(argc!=3)return 2;const std::string mode=argv[1];halTiltSensor.available=std::atoi(argv[2])!=0;
 if(mode=="reader-text" || mode=="reader-text-empty-heap") {
  // The actual toolbar lookup must fit after BLE and the saved page have fragmented RAM.
  const auto expected=getBaseSettingsList();
  releaseBaseSettingsList();
  for(const char* key:readermenu::TEXT_KEYS) {
   const auto ref=std::find_if(expected.begin(),expected.end(),[&](const SettingInfo& r){return r.key&&std::strcmp(r.key,key)==0;});
   if(ref==expected.end())return 15;
   const ptrdiff_t field=ref->valuePtr?reinterpret_cast<const char*>(&(SETTINGS.*(ref->valuePtr)))-reinterpret_cast<const char*>(&SETTINGS):-1;
   std::printf("text_schema key=%s name=%d type=%d field=%td category=%d text=%d obfuscated=%d range=%u/%u/%u labels=",key,static_cast<int>(ref->nameId),static_cast<int>(ref->type),field,static_cast<int>(ref->category),ref->inTextSettings,ref->obfuscated,ref->valueRange.min,ref->valueRange.max,ref->valueRange.step);
   for(const auto label:ref->enumLabels())std::printf("%d,",static_cast<int>(label));
   std::puts("");
  }
  mem::rejectAlloc=mode=="reader-text-empty-heap";
  mem::start();
  bool ok=true;
  try { for(int repeat=0;repeat<3;++repeat) for(int id=-1;id<=readermenu::TEXT_KEY_COUNT;++id) {
   const auto row=catalogTextRow(id);
   if(id<5 || id>=readermenu::TEXT_KEY_COUNT) {ok=ok&&!row;continue;}
   const auto ref=std::find_if(expected.begin(),expected.end(),[&](const SettingInfo& r){return r.key&&std::strcmp(r.key,readermenu::TEXT_KEYS[id])==0;});
   if(!row || ref==expected.end()) {ok=false;continue;}
   ok=ok&&row->nameId==ref->nameId&&row->type==ref->type&&row->valuePtr==ref->valuePtr;
   ok=ok&&std::strcmp(row->key,ref->key)==0&&row->category==ref->category&&row->inTextSettings==ref->inTextSettings;
   ok=ok&&row->obfuscated==ref->obfuscated&&row->action==ref->action&&row->stringOffset==ref->stringOffset&&row->stringMaxLen==ref->stringMaxLen;
   ok=ok&&row->enumStringValues==ref->enumStringValues&&!row->valueGetter&&!row->valueSetter&&!row->stringGetter&&!row->stringSetter;
   const auto labels=row->enumLabels(), want=ref->enumLabels();
   ok=ok&&labels.size()==want.size()&&std::equal(labels.begin(),labels.end(),want.begin());
   ok=ok&&row->valueRange.min==ref->valueRange.min&&row->valueRange.max==ref->valueRange.max&&row->valueRange.step==ref->valueRange.step;
   ok=ok&&settings_catalog::storage().empty();
  }} catch(const std::bad_alloc&) {ok=false;std::puts("reader_text_heap_exhausted=RED");}
  mem::report("reader_text_cold_and_reopen",0,0);
  ok=ok&&mem::count==0&&mem::peak==0&&mem::live==0;
  std::printf("reader_text_no_resident_catalog=%s\n",ok?"GREEN":"RED");
  return ok?0:14;
 }
 if(mode=="cold") {mem::start();const auto&base=getBaseSettingsList();mem::report("cold",base.size(),base.capacity());bool ok=base.size()==base.capacity(); size_t tiltRows=0; for(const auto&row:base) if(row.nameId==StrId::STR_TILT_PAGE_TURN)++tiltRows; ok=ok && tiltRows==static_cast<size_t>(halTiltSensor.available);std::printf("capacity_exact=%s\n",ok?"GREEN":"RED");return ok?0:1;}
 const auto&base=getBaseSettingsList();(void)I18N.get(StrId::STR_FONT_FAMILY);
 SdCardFontRegistry registry;auto&families=const_cast<std::vector<SdCardFontFamilyInfo>&>(registry.getFamilies());
 families.push_back({"SD font family name number 1",{"font"},{{10,0,0},{14,0,0}},true});
 families.push_back({"SD font family name number 2",{"font"},{{12,0,0},{16,0,0}},true});
 std::vector<DictionaryEntry> dictionaries; dictionaries.push_back({"Long dictionary folder name for allocation one"}); dictionaries.push_back({"Long dictionary folder name for allocation two"});
 if(mode=="home" || mode=="home-file") {
  auto&pins=menucustom::state();pins.pinCount=mode=="home"?3:1;
  strcpy(pins.pins[0].data(),mode=="home"?"settings/uiTextSize":"bookid/test");
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
  mem::start();{auto id=menufavorites::label("settings/uiTextSize");if(id!=StrId::STR_UI_TEXT_SIZE)return 3;auto value=menufavorites::value("settings/uiTextSize",&registry);if(value.empty())return 7;mem::report("one_favorite_direct",1,0);}
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
 if(mode=="json") {JsonDocument doc;SETTINGS.toJson(doc);mem::start();SETTINGS.toJson(doc);mem::report("save_warm",0,0);if(mem::live!=0||mem::peak>=1024)return 8;mem::start();bool ok=SETTINGS.fromJson(doc.as<JsonVariantConst>());mem::report("load_warm",0,0);return ok && mem::live==0 && mem::peak<1024?0:5;}  // v1.0.53: one descriptor at a time, nothing kept
 if(mode=="v108") {
  auto expect=[](bool value,const char* label){if(!value)std::printf("FAIL %s\n",label);return value;};
  const auto uiSize=std::find_if(base.begin(),base.end(),[](const SettingInfo& row){return row.key && std::strcmp(row.key,"uiTextSize")==0;});
  const auto inkWeight=std::find_if(base.begin(),base.end(),[](const SettingInfo& row){return row.key && std::strcmp(row.key,"readerInkWeight")==0;});
  bool ok=expect(uiSize!=base.end() && uiSize->type==SettingType::ENUM && uiSize->enumValues.size()==3,"ui size catalog");
  ok=expect(inkWeight!=base.end() && inkWeight->type==SettingType::ENUM && inkWeight->enumLabels().size()==4,"ink catalog")&&ok;
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
 if(mode=="wake-card") {
  // The retired wakeButtons key (a card from an earlier release stores 3) must
  // load, change nothing, and be dropped by the next save.
  JsonDocument plain;SETTINGS.toJson(plain);plain.remove("wakeButtons");
  std::string plainText;serializeJson(plain,plainText);
  bool ok=SETTINGS.fromJson(plain.as<JsonVariantConst>());
  JsonDocument reference;SETTINGS.toJson(reference);std::string referenceText;serializeJson(reference,referenceText);
  JsonDocument card;deserializeJson(card,plainText);card["wakeButtons"]=3;
  ok=SETTINGS.fromJson(card.as<JsonVariantConst>())&&ok;
  JsonDocument after;SETTINGS.toJson(after);std::string afterText;serializeJson(after,afterText);
  if(afterText!=referenceText)std::printf("FAIL card with wakeButtons=3 saves differently from the same card without it\n");
  if(after["wakeButtons"].is<int>())std::printf("FAIL next save still writes wakeButtons=%d\n",after["wakeButtons"].as<int>());
  ok=ok&&afterText==referenceText&&!after["wakeButtons"].is<int>();
  std::printf("retired_wake_key=%s\n",ok?"GREEN":"RED");return ok?0:13;
 }
 return 2;
}
