#pragma once
#include <ArduinoJson.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
using String = std::string;
struct IoCounts {
  size_t opens=0, directoryOpens=0, entryOpens=0, fullPathFileOpens=0;
  size_t readCalls=0, bytesRead=0, readFiles=0, parses=0, readDocs=0, exists=0;
  size_t removes=0, renames=0, mutationsWithOpenHandle=0, yieldedMs=0;
};
inline IoCounts io;
inline std::string fixtureRoot;
inline std::unordered_map<std::string,size_t> openEntries;
// Faults apply to the directory entry handle, so an accidental legacy fallback
// would succeed and the integrated tests can detect stale data promotion.
struct ReadFault {
  std::string path;
  size_t shortLimit=0, failAfter=SIZE_MAX, extraSize=0;
  bool closeFails=false;
  int result=-1; // -1: signed error, 0: premature EOF, 1: overlong result.
  bool fullPath=false, openFails=false;
  size_t hits=0;
};
inline ReadFault readFault;
enum class StorageFaultOperation { None, OpenWrite, Write, CloseWrite, Remove, Rename };
struct StorageFault {
  StorageFaultOperation operation=StorageFaultOperation::None;
  std::string path, target;
  size_t hits=0;
};
inline StorageFault storageFault;
inline void delay(unsigned ms) { io.yieldedMs += ms; }
#define LOG_ERR(...) ((void)0)
class HalFile {
  struct Handle {
    FILE* fp=nullptr; DIR* dp=nullptr; std::string name;
    bool entry=false, countedRead=false, writable=false;
    size_t offset=0;
    ~Handle(){ if(fp) fclose(fp); if(dp) closedir(dp); if(entry) --openEntries[name]; }
  };
  std::shared_ptr<Handle> h;
 public:
  HalFile()=default;
  HalFile(const std::string& path, int flags=O_RDONLY, bool entry=false) {
    ++io.opens;
    const bool writable=flags & O_WRONLY;
    if (!entry && writable && storageFault.operation==StorageFaultOperation::OpenWrite &&
        path==fixtureRoot+storageFault.path) { ++storageFault.hits; return; }
    if (!entry && !(flags & O_WRONLY) && readFault.fullPath && readFault.openFails &&
        path==fixtureRoot+readFault.path) { ++readFault.hits; return; }
    auto p=std::make_shared<Handle>(); p->name=path; p->entry=entry; p->writable=writable;
    if(entry) ++openEntries[path];
    struct stat st{};
    if (!stat(path.c_str(), &st) && S_ISDIR(st.st_mode)) {
      ++io.directoryOpens; p->dp=opendir(path.c_str());
    } else {
      if(entry) ++io.entryOpens; else ++io.fullPathFileOpens;
      p->fp=fopen(path.c_str(), (flags & O_WRONLY) ? "wb" : "rb");
    }
    if (p->dp || p->fp) h=p;
  }
  explicit operator bool() const { return bool(h); }
  bool isDirectory() const { return h && h->dp; }
  bool faultMatches() const { return h && (h->entry || readFault.fullPath) && !readFault.path.empty() &&
    (readFault.path=="*" || h->name==fixtureRoot+readFault.path); }
  size_t size() const {
    struct stat st{};
    return h && !stat(h->name.c_str(), &st) ? st.st_size+(faultMatches()?readFault.extraSize:0) : 0;
  }
  HalFile openNextFile() {
    if (!h || !h->dp) return {};
    while (auto* e=readdir(h->dp)) {
      if (strcmp(e->d_name,".") && strcmp(e->d_name,"..")) return HalFile(h->name+"/"+e->d_name,O_RDONLY,true);
    }
    return {};
  }
  void getName(char* out, size_t n) const { snprintf(out,n,"%s",std::filesystem::path(h->name).filename().c_str()); }
  int read(void* out,size_t requested) {
    ++io.readCalls;
    if(!h || !h->fp) return -1;
    if(!h->countedRead) { ++io.readFiles; h->countedRead=true; }
    size_t count=requested;
    if(faultMatches()) {
      if(h->offset>=readFault.failAfter) { ++readFault.hits; return readFault.result==1 ? static_cast<int>(requested+1) : readFault.result; }
      count=std::min(count,readFault.failAfter-h->offset);
      if(readFault.shortLimit) count=std::min(count,readFault.shortLimit);
    }
    const size_t n=fread(out,1,count,h->fp);
    h->offset+=n; io.bytesRead+=n;
    return ferror(h->fp) ? -1 : static_cast<int>(n);
  }
  size_t write(const char* p,size_t n) {
    if(h && h->writable && storageFault.operation==StorageFaultOperation::Write &&
       h->name==fixtureRoot+storageFault.path) { ++storageFault.hits; return 0; }
    return h && h->fp ? fwrite(p,1,n,h->fp) : 0;
  }
  bool close() {
    const bool readFail=faultMatches() && readFault.closeFails;
    const bool writeFail=h && h->writable && storageFault.operation==StorageFaultOperation::CloseWrite &&
                         h->name==fixtureRoot+storageFault.path;
    if(readFail) ++readFault.hits;
    if(writeFail) ++storageFault.hits;
    h.reset(); return !readFail && !writeFail;
  }
};
struct FakeStorage {
  HalFile open(const char* p,int flags=O_RDONLY) const { return HalFile(fixtureRoot+p,flags); }
  bool exists(const char* p) const { ++io.exists; return std::filesystem::exists(fixtureRoot+p); }
  bool remove(const char* p) const {
    ++io.removes;
    if(storageFault.operation==StorageFaultOperation::Remove && storageFault.path==p) {
      ++storageFault.hits; return false;
    }
    if(openEntries[fixtureRoot+p]) ++io.mutationsWithOpenHandle;
    return std::filesystem::remove(fixtureRoot+p);
  }
  bool rename(const char* a,const char* b) const {
    ++io.renames;
    if(storageFault.operation==StorageFaultOperation::Rename && storageFault.path==a && storageFault.target==b) {
      ++storageFault.hits; return false;
    }
    if(openEntries[fixtureRoot+a] || openEntries[fixtureRoot+b]) ++io.mutationsWithOpenHandle;
    return ::rename((fixtureRoot+a).c_str(),(fixtureRoot+b).c_str())==0;
  }
  bool ensureDirectoryExists(const char* p) const { std::filesystem::create_directories(fixtureRoot+p); return true; }
  bool mkdir(const char* p) const { return ensureDirectoryExists(p); }
  std::string readFile(const char* p) const {
    if(readFault.fullPath && readFault.path==p) {
      auto file=open(p); if(!file) return {};
      size_t remaining=file.size(); std::string result; char buffer[512];
      while(remaining) {
        const size_t requested=std::min(remaining,sizeof(buffer));
        const int n=file.read(buffer,requested);
        if(n<=0 || static_cast<size_t>(n)>requested) { file.close(); return {}; }
        result.append(buffer,static_cast<size_t>(n)); remaining-=static_cast<size_t>(n);
      }
      return file.close() ? result : std::string{};
    }
    ++io.opens; ++io.fullPathFileOpens;
    auto* f=fopen((fixtureRoot+p).c_str(),"rb"); if(!f) return {};
    ++io.readFiles;
    std::string s; char b[512]; size_t n;
    do { ++io.readCalls; n=fread(b,1,sizeof(b),f); s.append(b,n); io.bytesRead+=n; } while(n==sizeof(b));
    fclose(f); return s;
  }
  bool writeFile(const char* p,const std::string& s) const {
    auto f=open(p,O_WRONLY|O_CREAT|O_TRUNC); return f && f.write(s.data(),s.size())==s.size();
  }
};
inline FakeStorage Storage;
