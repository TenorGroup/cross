#define main measure_dataset_main
#include "measure.cpp"
#undef main
#include "BookStatsLibraryActivity.h"
#include <functional>
void require(bool result,const char* message){if(!result){std::cerr<<"FAIL: "<<message<<"\n";std::exit(1);}}
void resetMemory(){JsonDocument doc;doc["schema"]=3;require(READING_STATS.fromJson(doc.as<JsonVariantConst>()),"reset in-memory state");READING_STATS.statisticsReadable=true;}
void report(unsigned n,const char* label,IoCounts c,long long us) {
  std::cout<<"{\"records\":"<<n<<",\"page\":\""<<label<<"\",\"opens\":"<<c.opens<<",\"read_calls\":"<<c.readCalls<<",\"legacy_parses\":"<<c.parses<<",\"bytes_read\":"<<c.bytesRead<<",\"latency_us\":"<<us<<",\"requested_yield_ms\":"<<c.yieldedMs<<"}"<<std::endl;
}
void measuredNavigation(unsigned n) {
  fixture(n);resetMemory();
  GfxRenderer r;MappedInputManager i;BookStatsLibraryActivity a(r,i);
  auto run=[&](const char* name,const std::function<void()>& action){io={};auto begin=std::chrono::steady_clock::now();action();report(n,name,io,std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count());};
  run("first",[&]{a.onEnter();});auto first=a.labels();
  run("next",[&]{a.click(a.rowCount()-1);});auto second=a.labels();
  run("previous",[&]{a.click(0);});require(first==a.labels(),"previous page contents differ");
  require(io.opens==0 && io.readCalls==0 && io.entryOpens==0,"revisiting adjacent page rereads all history");
  run("next-again",[&]{a.click(a.rowCount()-1);});require(second==a.labels(),"next page contents differ");
  require(io.opens==0 && io.readCalls==0 && io.entryOpens==0,"next adjacent cache miss");
}
void invalidation(const char* name,const std::function<void()>& mutate,bool active=false){
  fixture(100);resetMemory();
  if(active){require(READING_STATS.activateBook("/Books/active.epub",0,"Active title"),"activate seed");READING_STATS.record(20260901,1000,1,1);}
  std::vector<Entry> first;READING_STATS.listBooks({},false,first);first.pop_back();
  std::vector<Entry> second;READING_STATS.listBooks(first.back(),false,second);second.pop_back();
  GfxRenderer r;MappedInputManager i;BookStatsLibraryActivity a(r,i);a.onEnter();a.click(a.rowCount()-1);
  mutate();
  std::vector<Entry> oracle;READING_STATS.listBooks(second.front(),true,oracle);
  std::vector<std::string> expected;
  if(oracle.size()>20){oracle.erase(oracle.begin());expected.push_back("Previous");}
  for(auto& b:oracle)expected.push_back(b.title);
  if(!oracle.empty())expected.push_back("Next");
  if(expected.empty())expected.push_back(READING_STATS.statisticsReadable ? "No stats" : "Read error");
  io={};a.click(0);
  require(io.opens>0 || !READING_STATS.statisticsReadable,"generation mutation retained stale cached page");
  require(a.labels()==expected,"invalidated navigation differs from fresh production scan");
  if(oracle.empty()){a.click(0);require(a.labels()==expected,"empty page action changed view");}
  std::cout<<"PASS invalidation "<<name<<"\n";
}
void oversizePage(){
  fixture(100);resetMemory();
  for(const auto& item:std::filesystem::directory_iterator(fixtureRoot+"/.crosspoint/reading-stats")){
    JsonDocument doc;std::ifstream in(item.path());deserializeJson(doc,in);in.close();doc["title"]=std::string(240,'T');std::ofstream out(item.path());serializeJson(doc,out);
  }
  GfxRenderer r;MappedInputManager i;BookStatsLibraryActivity a(r,i);a.onEnter();auto first=a.labels();a.click(a.rowCount()-1);io={};a.click(0);
  require(io.readFiles==100,"oversized page retained in cache");require(a.labels()==first,"oversized page content mismatch");
  std::cout<<"PASS oversized page bypasses bounded cache\n";
}
void oversizeSwap(){
  fixture(100);resetMemory();
  std::vector<Entry> first;READING_STATS.listBooks({},false,first);first.pop_back();
  for(const auto& item:std::filesystem::directory_iterator(fixtureRoot+"/.crosspoint/reading-stats")){
    JsonDocument doc;std::ifstream in(item.path());deserializeJson(doc,in);in.close();const std::string path=doc["path"]|"";
    bool onFirst=false;for(const auto& entry:first)onFirst|=entry.path==path;
    if(!onFirst){doc["title"]=std::string(240,'T');std::ofstream out(item.path());serializeJson(doc,out);}
  }
  GfxRenderer r;MappedInputManager i;BookStatsLibraryActivity a(r,i);a.onEnter();auto labels=a.labels();a.click(a.rowCount()-1);
  io={};a.click(0);require(io.opens==0 && io.readCalls==0 && io.entryOpens==0,"small cached first page missed");require(labels==a.labels(),"swap changed cached first page");
  io={};a.click(a.rowCount()-1);require(io.readFiles==100,"oversized current page entered cache during swap");
  std::cout<<"PASS oversized current page is discarded after cache swap\n";
}
void recovery(){
  fixture(100);resetMemory();
  std::vector<Entry> before;READING_STATS.listBooks({},false,before);
  const auto path=fileFor(before.front().path);
  std::filesystem::copy_file(fixtureRoot+path,fixtureRoot+path+".bak");Storage.writeFile(path.c_str(),"{broken");
  std::vector<Entry> after;READING_STATS.listBooks({},false,after);
  require(after.size()==before.size(),"backup recovery lost entry");for(size_t i=0;i<before.size();++i)require(after[i].path==before[i].path,"backup recovery changed order");
  require(READING_STATS.activateBook("/Books/active.epub",0,"Active title"),"recovery activate");
  READING_STATS.record(20260919,60000,10,4);require(READING_STATS.saveToFile(),"recovery first save");require(READING_STATS.saveToFile(),"recovery second save");
  Storage.writeFile(ReadingStatsStore::getFilePath(),"{broken");require(READING_STATS.loadFromFile(),"global backup recovery");
  require(READING_STATS.activeBook.minutes==1 && READING_STATS.activeBook.turns==10,"global backup lost counters");
  std::cout<<"PASS per-book and global backup recovery\n";
}
int main(){
  for(unsigned n:{100u,1000u,5000u})measuredNavigation(n);
  invalidation("activate",[]{require(READING_STATS.activateBook("/Books/new.epub",0,"New title"),"activate mutation");});
  invalidation("title",[]{require(READING_STATS.activateBook("/Books/active.epub",0,"Changed title"),"title mutation");},true);
  invalidation("day",[]{READING_STATS.record(20260930,1000,1,2);},true);
  invalidation("fromJson",[]{JsonDocument d;d["schema"]=3;READING_STATS.fromJson(d.as<JsonVariantConst>());});
  invalidation("reset-all",[]{require(READING_STATS.resetStatistics(true),"reset all");});
  invalidation("reset-habits",[]{require(READING_STATS.resetStatistics(false),"reset habits");});
  invalidation("load",[]{require(READING_STATS.saveToFile(),"save before load");require(READING_STATS.loadFromFile(),"load after save");});
  invalidation("load-failed",[]{require(!READING_STATS.loadFromFile(),"expected unavailable global snapshot");});
  oversizePage();
  oversizeSwap();
  recovery();
  std::cout<<"PASS integrated navigation and generation cases\n";
}
