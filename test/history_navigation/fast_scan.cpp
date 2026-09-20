#define main measure_dataset_main
#include "measure.cpp"
#undef main
#include "ReadingStatsStore.cpp"
#include <sstream>
#include <stdexcept>
void check(bool pass,const char* message) { if(!pass) throw std::runtime_error(message); }
void resetScan() {
  readFault={}; JsonDocument doc;doc["schema"]=3;
  check(READING_STATS.fromJson(doc.as<JsonVariantConst>()),"reset memory");
  READING_STATS.statisticsReadable=true;
}
uint64_t contentHash(const std::vector<Entry>& entries) {
  uint64_t hash=14695981039346656037ULL;
  for(const auto& b:entries) {
    const std::string s=b.path+"\n"+b.title+"\n"+std::to_string(b.day)+"\n";
    for(unsigned char c:s) { hash^=c; hash*=1099511628211ULL; }
  }
  return hash;
}
std::vector<Entry> expected(unsigned n) {
  std::vector<Entry> result;
  for(unsigned i=0;i<n;++i) {
    char suffix[20];snprintf(suffix,sizeof(suffix),"%05u",i);
    result.push_back({std::string("/Books/Van hoc va lich su/Tuyen tap tac pham ")+suffix+".epub",
      std::string("Tuyen tap tac pham ")+suffix+" - Nhat ky doc sach",20260901+i%18});
  }
  std::sort(result.begin(),result.end(),[](const Entry& a,const Entry& b){return a.day!=b.day?a.day>b.day:a.path<b.path;});
  if(result.size()>21)result.resize(21);return result;
}
constexpr size_t acceptedReadChunk=256;
void padSnapshots(size_t padding) {
  if(!padding)return;
  const auto directory=fixtureRoot+"/.crosspoint/reading-stats";
  for(const auto& item:std::filesystem::directory_iterator(directory)) {
    std::ofstream out(item.path(),std::ios::app);out<<std::string(padding,' ');
  }
}
size_t readCallBound() {
  size_t calls=0;
  const auto directory=fixtureRoot+"/.crosspoint/reading-stats";
  for(const auto& item:std::filesystem::directory_iterator(directory)) {
    const auto size=std::filesystem::file_size(item.path());
    calls+=(size+acceptedReadChunk-1)/acceptedReadChunk;
  }
  return calls;
}
void measurement(bool gate) {
  struct Shape { const char* name;size_t padding; };
  for(const auto shape:{Shape{"small",0},Shape{"large",768}}) for(unsigned n:{100u,1000u,5000u}) {
    fixture(n);padSnapshots(shape.padding);const auto maxReadCalls=readCallBound();resetScan();io={};std::vector<Entry> result;
    auto start=std::chrono::steady_clock::now();READING_STATS.listBooks({},false,result);
    auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    const auto hash=contentHash(result);
    std::cout<<"{\"records\":"<<n<<",\"record_shape\":\""<<shape.name<<"\",\"opens\":"<<io.opens<<",\"directory_opens\":"<<io.directoryOpens
      <<",\"entry_opens\":"<<io.entryOpens<<",\"full_path_file_opens\":"<<io.fullPathFileOpens
      <<",\"read_doc_calls\":"<<io.readDocs<<",\"exists\":"<<io.exists<<",\"read_calls\":"<<io.readCalls
      <<",\"read_call_bound\":"<<maxReadCalls
      <<",\"read_files\":"<<io.readFiles<<",\"bytes_read\":"<<io.bytesRead<<",\"legacy_parses\":"<<io.parses
      <<",\"entries\":"<<result.size()<<",\"content_hash\":\""<<std::hex<<hash<<std::dec
      <<"\",\"host_latency_us\":"<<us<<"}"<<std::endl;
    check(result.size()==21 && hash==contentHash(expected(n)),"21 results, sort order or content changed");
    if(gate) {
      check(io.directoryOpens==1 && io.entryOpens==n,"directory enumeration changed");
      check(io.fullPathFileOpens==0,"healthy entries reopened by full path");
      check(io.readDocs==0 && io.exists==0,"healthy entries reenter legacy recovery/probe");
      check(io.readCalls<=maxReadCalls,"healthy snapshots exceeded the 256-byte read-call budget");
    }
  }
}

std::string bytes(const std::string& path) {
  std::ifstream in(fixtureRoot+path,std::ios::binary);
  return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
void put(const std::string& path,const std::string& value) {
  std::ofstream out(fixtureRoot+path,std::ios::binary);out<<value;
}
std::string oneBook() { fixture(1);resetScan();return fileFor(expected(1).front().path); }
std::vector<Entry> scan() { std::vector<Entry> result;READING_STATS.listBooks({},false,result);return result; }
void preserveUnavailable(const std::string& path,const std::string& main,const char* name) {
  put(path+".bak",main);put(path+".davbak",main);
  io={};const auto result=scan();
  check(result.empty(),"unavailable main accepted or stale backup promoted");
  check(io.readDocs==0 && io.fullPathFileOpens==0,"unavailable entry used recovery path");
  check(io.removes==0 && io.renames==0,"unavailable entry mutated storage");
  check(bytes(path)==main && bytes(path+".bak")==main && bytes(path+".davbak")==main,"unavailable read changed files");
  std::cout<<"PASS unavailable "<<name<<"\n";
}
void faults() {
  {
    const auto path=oneBook();const auto original=bytes(path);readFault.path=path;readFault.shortLimit=7;
    io={};const auto result=scan();
    check(contentHash(result)==contentHash(expected(1)),"positive short reads changed content");
    check(io.readCalls>5 && io.readDocs==0 && io.fullPathFileOpens==0,"positive short read did not use buffered entry");
    check(io.bytesRead==original.size(),"positive short reads missed bytes");
    std::cout<<"PASS positive short reads\n";
  }
  for(int failure:{-1,0,1}) for(bool afterObject:{false,true}) {
    const auto path=oneBook();const auto original=bytes(path);const auto padded=original+std::string(600,' ');put(path,padded);
    readFault.path=path;readFault.failAfter=afterObject?original.size()+19:0;readFault.result=failure;
    preserveUnavailable(path,padded,(std::to_string(failure)+(afterObject?" after complete object":" before object")).c_str());
  }
  for(int failure:{-1,0,1}) {
    const auto path=oneBook();const auto invalid=std::string("{broken")+std::string(600,' ');put(path,invalid);
    readFault.path=path;readFault.failAfter=300;readFault.result=failure;
    preserveUnavailable(path,invalid,(std::to_string(failure)+" after invalid prefix").c_str());
  }
  {
    const auto path=oneBook();const auto original=bytes(path);readFault.path=path;readFault.extraSize=19;
    preserveUnavailable(path,original,"declared size exceeds valid object bytes");
  }
  {
    const auto path=oneBook();const auto original=bytes(path);readFault.path=path;readFault.closeFails=true;
    preserveUnavailable(path,original,"close failure");
  }
  {
    const auto path=oneBook();put(path,std::string(4097,' '));io={};
    check(scan().empty(),"oversize snapshot accepted");
    check(io.readCalls==0 && io.readDocs==0,"oversize snapshot read before size gate");
    std::cout<<"PASS 4096 byte bound\n";
  }
  for(const auto& kind:std::vector<std::string>{"malformed-bak","malformed-davbak","nonobject-bak"}) {
    const auto path=oneBook();const auto original=bytes(path);
    JsonDocument old;deserializeJson(old,original);old["title"]="Old davbak title";std::string oldJson;serializeJson(old,oldJson);
    put(path,kind=="nonobject-bak"?"[1,2,3]":"{broken");
    put(path+".bak",original);
    if(kind=="malformed-davbak")put(path+".davbak",original);
    if(kind=="nonobject-bak")put(path+".davbak",oldJson);
    io={};const auto result=scan();
    check(contentHash(result)==contentHash(expected(1)),"recovery changed entry or sort keys");
    check(io.readDocs>0,"invalid entry bypassed legacy recovery");
    check(io.mutationsWithOpenHandle==0,"recovery mutated path while its entry handle was open");
    check(bytes(path+".bak")==original,"recovery changed book backup");
    if(kind=="malformed-davbak")check(bytes(path)==original && !std::filesystem::exists(fixtureRoot+path+".davbak"),"davbak recovery lost promotion semantics");
    if(kind=="nonobject-bak")check(bytes(path)=="[1,2,3]" && bytes(path+".davbak")==oldJson,"nonobject recovery wrongly promoted davbak");
    std::cout<<"PASS recovery "<<kind<<"\n";
  }
  {
    const auto path=oneBook();const auto original=bytes(path);put(path+".bak",original);
    std::filesystem::remove(fixtureRoot+path);io={};const auto result=scan();
    check(contentHash(result)==contentHash(expected(1)),"missing main lost valid bak entry");
    check(io.fullPathFileOpens==0 && io.readDocs==0,"valid standalone bak reentered legacy path");
    check(bytes(path+".bak")==original && !std::filesystem::exists(fixtureRoot+path),"standalone bak unexpectedly promoted");
    std::cout<<"PASS missing main uses valid bak handle\n";
  }
  {
    const auto path=oneBook();const auto original=bytes(path);JsonDocument old;deserializeJson(old,original);
    old["title"]="Old backup title";old["last"]=20000101;std::string oldJson;serializeJson(old,oldJson);put(path+".bak",oldJson);
    io={};const auto result=scan();check(contentHash(result)==contentHash(expected(1)),"stale bak displaced healthy main");
    check(io.readDocs==0 && io.fullPathFileOpens==0 && bytes(path+".bak")==oldJson,"healthy main triggered backup recovery");
    std::cout<<"PASS healthy main keeps precedence over stale bak\n";
  }
  for(bool epoch:{false,true}) {
    const auto path=oneBook();JsonDocument wrong;deserializeJson(wrong,bytes(path));
    if(epoch)wrong["bookEpoch"]=1;else wrong["path"]="/Books/wrong.epub";
    std::string encoded;serializeJson(wrong,encoded);put(path,encoded);io={};
    check(scan().empty(),"epoch or path hash mismatch accepted");
    check(io.readDocs==0 && io.fullPathFileOpens==0,"valid mismatched metadata used recovery");
    std::cout<<"PASS semantic filter "<<(epoch?"epoch":"path hash")<<"\n";
  }
  readFault={};
}
#ifdef HISTORY_HAS_OPEN_SNAPSHOT
struct FailAllocator: ArduinoJson::Allocator {
  size_t allowed,attempts=0,failed=0;
  explicit FailAllocator(size_t permitted):allowed(permitted) {}
  bool deny() { ++attempts;if(allowed) { --allowed;return false; }++failed;return true; }
  void* allocate(size_t size) override { return deny()?nullptr:malloc(size); }
  void deallocate(void* ptr) override { free(ptr); }
  void* reallocate(void* ptr,size_t size) override { return deny()?nullptr:realloc(ptr,size); }
};
void allocationFailures() {
  for(size_t permitted:{0u,1u}) {
    const auto path=oneBook();const auto original=bytes(path);put(path+".bak",original);put(path+".davbak",original);
    HalFile file(fixtureRoot+path,O_RDONLY,true);const auto size=file.size();FailAllocator allocator(permitted);JsonDocument doc(&allocator);
    io={};const auto status=readOpenSnapshot(file,size,doc);
    check(allocator.failed>0,"allocator fixture did not reach failure");
    check(status==SnapshotReadResult::Unavailable,"NoMemory classified as corruption or Ready");
    check(!file,"NoMemory kept directory entry open");
    check(io.readDocs==0 && io.removes==0 && io.renames==0,"NoMemory helper mutated storage");
    check(bytes(path)==original && bytes(path+".bak")==original && bytes(path+".davbak")==original,"NoMemory lost a snapshot");
    std::cout<<"PASS allocator fail after "<<permitted<<" allocations\n";
  }
}
#endif
int main(int argc,char** argv) {
  try {
    const std::string mode=argc>1?argv[1]:"measure";
    if(mode=="faults")faults();
    else if(mode=="oom") {
#ifdef HISTORY_HAS_OPEN_SNAPSHOT
      allocationFailures();
#else
      throw std::runtime_error("baseline has no open-entry helper for allocator injection");
#endif
    } else measurement(mode=="contract");
    return 0;
  } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n";return 1; }
}
