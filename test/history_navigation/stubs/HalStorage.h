#pragma once
#include <ArduinoJson.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
using String = std::string;
struct IoCounts { size_t opens=0, readCalls=0, bytesRead=0, parses=0, yieldedMs=0; };
inline IoCounts io;
inline std::string fixtureRoot;
inline void delay(unsigned ms) { io.yieldedMs += ms; }
#define LOG_ERR(...) ((void)0)
class HalFile {
  struct Handle {
    FILE* fp=nullptr; DIR* dp=nullptr; std::string name;
    ~Handle(){ if(fp) fclose(fp); if(dp) closedir(dp); }
  };
  std::shared_ptr<Handle> h;
 public:
  HalFile()=default;
  HalFile(const std::string& path, int flags=O_RDONLY) {
    ++io.opens;
    auto p=std::make_shared<Handle>(); p->name=path;
    struct stat st{};
    if (!stat(path.c_str(), &st) && S_ISDIR(st.st_mode)) p->dp=opendir(path.c_str());
    else p->fp=fopen(path.c_str(), (flags & O_WRONLY) ? "wb" : "rb");
    if (p->dp || p->fp) h=p;
  }
  explicit operator bool() const { return bool(h); }
  bool isDirectory() const { return h && h->dp; }
  size_t size() const { struct stat st{}; return h && !stat(h->name.c_str(), &st) ? st.st_size : 0; }
  HalFile openNextFile() {
    if (!h || !h->dp) return {};
    while (auto* e=readdir(h->dp)) {
      if (strcmp(e->d_name,".") && strcmp(e->d_name,"..")) return HalFile(h->name+"/"+e->d_name);
    }
    return {};
  }
  void getName(char* out, size_t n) const { snprintf(out,n,"%s",std::filesystem::path(h->name).filename().c_str()); }
  size_t write(const char* p,size_t n) { return h && h->fp ? fwrite(p,1,n,h->fp) : 0; }
  bool close() { h.reset(); return true; }
};
struct FakeStorage {
  HalFile open(const char* p,int flags=O_RDONLY) const { return HalFile(fixtureRoot+p,flags); }
  bool exists(const char* p) const { return std::filesystem::exists(fixtureRoot+p); }
  bool remove(const char* p) const { return std::filesystem::remove(fixtureRoot+p); }
  bool rename(const char* a,const char* b) const { return ::rename((fixtureRoot+a).c_str(),(fixtureRoot+b).c_str())==0; }
  bool ensureDirectoryExists(const char* p) const { std::filesystem::create_directories(fixtureRoot+p); return true; }
  bool mkdir(const char* p) const { return ensureDirectoryExists(p); }
  std::string readFile(const char* p) const {
    ++io.opens; auto* f=fopen((fixtureRoot+p).c_str(),"rb"); if(!f) return {};
    std::string s; char b[512]; size_t n;
    do { ++io.readCalls; n=fread(b,1,sizeof(b),f); s.append(b,n); io.bytesRead+=n; } while(n==sizeof(b));
    fclose(f); return s;
  }
  bool writeFile(const char* p,const std::string& s) const {
    auto f=open(p,O_WRONLY|O_CREAT|O_TRUNC); return f && f.write(s.data(),s.size())==s.size();
  }
};
inline FakeStorage Storage;
