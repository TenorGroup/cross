#pragma once
#include <string>
#include <map>
using String=std::string;
struct MemoryFile {
  bool valid; size_t bytes;
  explicit operator bool() const { return valid; }
  size_t size() const { return bytes; }
  void close() {}
};
struct MemoryStorage {
  std::map<std::string,std::string> files;
  bool writes=true;
  MemoryFile open(const char* path) { return {files.count(path)>0,files.count(path)?files.at(path).size():0}; }
  String readFile(const char* path) { return files.count(path)?files.at(path):""; }
  bool writeFile(const char* path, const String& text) { if(!writes)return false; files[path]=text; return true; }
  void mkdir(const char*) {}
  bool exists(const char* path) { return files.count(path); }
  bool remove(const char* path) { return files.erase(path)>0; }
  bool rename(const char* from,const char* to) { if(!files.count(from))return false; files[to]=files.at(from); files.erase(from); return true; }
};
inline MemoryStorage Storage;
