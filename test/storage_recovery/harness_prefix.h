#pragma once
#include "Arduino.h"
#include <cassert>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <algorithm>
#include <cstring>
#include "Logging.h"
#include "PersistableStore.h"
#include "WebDavReplace.h"
struct SDCardManager;
struct FsFile {
 SDCardManager* store=nullptr; std::string path; size_t position=0;
 size_t fileSize();int read(void*,size_t);int read();bool available();
 size_t print(const String& s); size_t write(const uint8_t* s,size_t len); bool close();bool sync();
};
struct SDCardManager {
 bool initialized=true;
 std::map<std::string,std::string> files; std::set<std::string> failRename;
 bool failOpen=false,failWrite=false,failSync=false,failClose=false,failRemove=false,failRead=false; size_t shortRead=0;
 std::string readFaultPath;
 int readCount=0,readFaultHits=0;
 std::map<std::string,int> readsByPath;
 bool readFaultMatches(const std::string& path)const{return readFaultPath.empty()||readFaultPath==path;}
 SDCardManager& vol(){return *this;}
 bool exists(const char*p){return files.count(p);}
 bool remove(const char*p){return !failRemove && files.erase(p);}
 bool rename(const char*a,const char*b){if(failRename.count(a)||!exists(a)||exists(b))return false; files[b]=files[a];files.erase(a);return true;}
 bool openFileForWrite(const char*,const char*p,FsFile& f){if(failOpen)return false;files[p]=""; f={this,p};return true;}
 bool openFileForRead(const char*,const char*p,FsFile&f){if(failOpen&&readFaultMatches(p)){++readFaultHits;return false;}if(!exists(p))return false;f={this,p,0};return true;}
 bool mkdir(const char*){return true;}
 bool writeFile(const char*,const String&);
 String readFile(const char*p);
};
unsigned long millis(){return 0;}
size_t FsFile::fileSize(){return store->files[path].size();}
bool FsFile::available(){return position<fileSize();}
int FsFile::read(void*out,size_t n){++store->readCount;++store->readsByPath[path];if(store->failRead&&store->readFaultMatches(path)){++store->readFaultHits;return -1;}n=std::min(n,fileSize()-position);if(store->shortRead)n=std::min(n,store->shortRead);memcpy(out,store->files[path].data()+position,n);position+=n;return n;}
int FsFile::read(){char c;return read(&c,1)==1?static_cast<unsigned char>(c):-1;}
size_t FsFile::print(const String&s){return write(reinterpret_cast<const uint8_t*>(s.c_str()),s.length());}
size_t FsFile::write(const uint8_t*s,size_t n){if(store->failWrite)n/=2;store->files[path].append(reinterpret_cast<const char*>(s),n);return n;}
bool FsFile::close(){if(store->failClose&&store->readFaultMatches(path)){++store->readFaultHits;return false;}return true;}
bool FsFile::sync(){return !store->failSync;}
using HalFile=FsFile;
inline SDCardManager Storage;
struct Stream{};
#include "HttpDownloader.h"
struct Sink {std::function<bool(const uint8_t*,size_t)>write;HttpDownloader::ProgressCallback progress;bool*cancelFlag=nullptr;size_t total=0,downloaded=0;};
inline HttpDownloader::DownloadError transferResult=HttpDownloader::OK;
inline std::string transferBody="new-book";
HttpDownloader::DownloadError runGetSecure(const std::string&,const std::string&,const std::string&,Sink&s,bool){
 if(transferResult==HttpDownloader::HTTP_ERROR)return transferResult;
 if(!s.write(reinterpret_cast<const uint8_t*>(transferBody.data()),transferBody.size()))return HttpDownloader::FILE_ERROR;
 s.downloaded+=transferBody.size();return transferResult;
}
inline int invalidations=0;inline bool failInvalidation=false;
bool clearBookCache(const std::string&){++invalidations;return !failInvalidation;}
class ZipFile {public:explicit ZipFile(const std::string&){}bool open(){return true;}bool getInflatedFileSize(const char*,size_t*s){*s=1;return transferBody!="broken-epub";}bool close(){return true;}};
