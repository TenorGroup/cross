#include "ReadingStatsStore.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
std::string fileFor(const std::string& path) {
  uint64_t h=14695981039346656037ULL;
  for(unsigned char c:path){h^=c;h*=1099511628211ULL;}
  char name[80]; snprintf(name,sizeof(name),"/.crosspoint/reading-stats/tenor_%016llx.json",(unsigned long long)h); return name;
}
void fixture(unsigned n) {
  fixtureRoot="/tmp/tenor-history-perf-"+std::to_string(getpid())+"-"+std::to_string(n);
  std::filesystem::remove_all(fixtureRoot);
  std::filesystem::create_directories(fixtureRoot+"/.crosspoint/reading-stats");
  for(unsigned i=0;i<n;++i) {
    char suffix[20];snprintf(suffix,sizeof(suffix),"%05u",i);
    std::string path=std::string("/Books/Van hoc va lich su/Tuyen tap tac pham ")+suffix+".epub";
    JsonDocument doc;doc["bookEpoch"]=0;doc["path"]=path;doc["title"]=std::string("Tuyen tap tac pham ")+suffix+" - Nhat ky doc sach";
    doc["minutes"]=i*3;doc["ms"]=1234;doc["turns"]=i*8;doc["first"]=20260101;doc["last"]=20260901+i%18;
    doc["days"]=i%30+1;doc["progress"]=i%101;doc["startProgress"]=0;
    std::ofstream out(fixtureRoot+fileFor(path));serializeJson(doc,out);
  }
}
using Entry=ReadingStatsStore::BookEntry;
std::vector<Entry> page(unsigned n,const char* label,const Entry& boundary,bool back) {
  io={};std::vector<Entry> out;auto start=std::chrono::steady_clock::now();
  READING_STATS.listBooks(boundary,back,out);
  auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
  std::cout<<"{\"records\":"<<n<<",\"page\":\""<<label<<"\",\"opens\":"<<io.opens<<",\"read_calls\":"<<io.readCalls<<",\"parses\":"<<io.parses<<",\"bytes_read\":"<<io.bytesRead<<",\"latency_us\":"<<us<<",\"requested_yield_ms\":"<<io.yieldedMs<<",\"entries\":"<<out.size()<<"}"<<std::endl;
  if(out.size()>20){if(back)out.erase(out.begin());else out.pop_back();}return out;
}
int main(){for(unsigned n:{100u,1000u,5000u}){fixture(n);auto first=page(n,"first",{},false);auto next=page(n,"next",first.back(),false);auto previous=page(n,"previous",next.front(),true);if(previous.size()!=first.size())return 3;for(size_t i=0;i<first.size();++i)if(first[i].path!=previous[i].path)return 4;}return 0;}
